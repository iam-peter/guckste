// The recorder's pieces that need no screen: the format this Qt encodes
// and the cropping of frames.

#include "core/CursorCapture.h"
#include "core/Encoding.h"
#include "core/KeyListener.h"
#include "core/KeyOverlay.h"
#include "core/ScreenRecorder.h"
#include "core/StillKeeper.h"
#include "core/VideoExporter.h"
#include "core/VideoFrames.h"
#include "core/WindowFinder.h"
#include "core/SingleInstance.h"

#include <QAudioBufferInput>
#include <QAudioBufferOutput>
#include <QFileInfo>
#include <QMediaCaptureSession>
#include <QMediaPlayer>
#include <QTemporaryDir>
#include <QVideoFrameInput>
#include <QVideoSink>
#include <QGuiApplication>
#include <QPainter>
#include <QProcess>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QMimeType>
#include <QTest>
#include <QVideoFrame>

#include <atomic>
#include <cmath>
#include <thread>

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

    // YUV frames become images on the CPU, colors kept: NV12 as written
    // for the encoder, and planar 4:2:0 as decoders give it.
    void convertsYuvOnTheCpu()
    {
        QImage image(64, 32, QImage::Format_RGB32);
        const QList<QColor> colors{Qt::red, Qt::green, Qt::blue, QColor(128, 128, 128)};
        for (int i = 0; i < 4; ++i)
            for (int y = 0; y < 32; ++y)
                for (int x = 0; x < 16; ++x)
                    image.setPixelColor(i * 16 + x, y, colors[i]);
        const QImage back = VideoFrames::toImage(VideoFrames::toNV12(image));
        QCOMPARE(back.size(), image.size());
        const auto near = [](const QColor &a, const QColor &b) {
            return std::abs(a.red() - b.red()) <= 8 && std::abs(a.green() - b.green()) <= 8
                && std::abs(a.blue() - b.blue()) <= 8;
        };
        for (int i = 0; i < 4; ++i)
            QVERIFY2(near(back.pixelColor(i * 16 + 8, 16), colors[i]), qPrintable(back.pixelColor(i * 16 + 8, 16).name()));
        // A region only.
        const QImage part = VideoFrames::toImage(VideoFrames::toNV12(image), QRect(16, 0, 16, 8));
        QCOMPARE(part.size(), QSize(16, 8));
        QVERIFY(near(part.pixelColor(8, 4), Qt::green));

        // Planar, BT.601 video range: Y 81, U 90, V 240 is red.
        QVideoFrameFormat format(QSize(4, 4), QVideoFrameFormat::Format_YUV420P);
        format.setColorSpace(QVideoFrameFormat::ColorSpace_BT601);
        format.setColorRange(QVideoFrameFormat::ColorRange_Video);
        QVideoFrame planar(format);
        QVERIFY(planar.map(QVideoFrame::WriteOnly));
        for (int y = 0; y < 4; ++y)
            memset(planar.bits(0) + y * planar.bytesPerLine(0), 81, 4);
        for (int y = 0; y < 2; ++y) {
            memset(planar.bits(1) + y * planar.bytesPerLine(1), 90, 2);
            memset(planar.bits(2) + y * planar.bytesPerLine(2), 240, 2);
        }
        planar.unmap();
        const QColor red = VideoFrames::toImage(planar).pixelColor(1, 1);
        QVERIFY2(near(red, Qt::red), qPrintable(red.name()));
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
        recorder.keepStill();
        QCOMPARE(recorder.stillCount(), 1);
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
        // Written beside the recording, lossless, at its time in it.
        const Still still = recorder.stills().value(0);
        QCOMPARE(QFileInfo(still.path).absolutePath(), ScreenRecorder::stillsDirectory(path));
        QCOMPARE(QImage(still.path).size(), QSize(640, 360));
        QVERIFY2(still.ms > 1000 && still.ms < 2000, qPrintable(QString::number(still.ms)));
        qInfo("Recorded %lld ms to %s (%lld bytes)", recorder.duration(), qPrintable(path), QFileInfo(path).size());

        // Stills only: no video, the stills in the folder all the same.
        const QString stillsPath = QFileInfo(path).absolutePath() + u"/only/only.mp4"_s;
        QSignalSpy stillsFinished(&recorder, &ScreenRecorder::stillsFinished);
        ScreenRecorder::Options only;
        only.video = false;
        QVERIFY(recorder.start(QRect(100, 100, 320, 200), stillsPath, only));
        QTRY_VERIFY_WITH_TIMEOUT(!recorder.starting(), ScreenRecorder::warmUpMs + 500);
        QTest::qWait(800);
        recorder.keepStill();
        QVERIFY(recorder.duration() > 500);
        recorder.stop();
        QTRY_COMPARE_WITH_TIMEOUT(recorder.state(), ScreenRecorder::Idle, 5000);
        QCOMPARE(stillsFinished.count(), 1);
        QVERIFY(!QFileInfo::exists(stillsPath));
        QCOMPARE(QImage(recorder.stills().value(0).path).size(), QSize(320, 200));
    }

    static StillKeeper::Frame screen(const QColor &color, int ms)
    {
        StillKeeper::Frame frame;
        frame.image = QImage(16, 10, QImage::Format_RGB32);
        frame.image.fill(color);
        frame.ms = ms;
        return frame;
    }

    static QList<Still::Kind> kindsOf(const QList<Still> &stills)
    {
        QList<Still::Kind> kinds;
        for (const Still &still : stills)
            kinds << still.kind;
        return kinds;
    }

    // Around a click, the screen before it, and after it once the screen
    // settles; nothing after a click that changes nothing, and no picture
    // twice in a row unless asked for.
    void keepsStillsAroundClicks()
    {
        using enum Still::Kind;
        QTemporaryDir dir;
        StillKeeper keeper;
        keeper.begin(dir.filePath(u"rec stills"_s));
        keeper.press();
        QVERIFY(keeper.stills().isEmpty());

        int ms = 0;
        const auto run = [&](const QColor &color, int until) {
            for (; ms <= until; ms += 33)
                keeper.frame(screen(color, ms));
        };
        run(Qt::white, 99);
        StillKeeper::Frame hovered = screen(Qt::gray, ms);
        hovered.pointer = QImage(2, 2, QImage::Format_ARGB32_Premultiplied);
        hovered.pointerRect = QRectF(3, 4, 2, 2);
        keeper.frame(hovered);
        ms += 33;
        keeper.press();
        // An animation, then the result.
        run(Qt::darkGray, 300);
        run(Qt::black, 500);
        QCOMPARE(kindsOf(keeper.stills()), QList({BeforeClick}));
        run(Qt::black, 900);
        QCOMPARE(kindsOf(keeper.stills()), QList({BeforeClick, AfterClick}));
        QCOMPARE(keeper.stills()[0].ms, 132);
        QCOMPARE(keeper.stills()[0].pointerRect, QRectF(3, 4, 2, 2));
        QVERIFY(keeper.stills()[1].ms >= 330 + StillKeeper::settleMs);

        // Nothing changes: no still after, and none of the same before.
        keeper.press();
        run(Qt::black, 900 + StillKeeper::waitForChangeMs + 100);
        QCOMPARE(keeper.stills().size(), 2);
        // Asked for, it is kept all the same.
        keeper.keep();
        QCOMPARE(kindsOf(keeper.stills()), QList({BeforeClick, AfterClick, Kept}));

        // Clicked again while the screen still changes: what the first
        // click did so far ends it.
        keeper.press();
        run(Qt::red, ms + 100);
        keeper.press();
        QCOMPARE(kindsOf(keeper.stills()).mid(3), QList({AfterClick}));
        // A screen that never settles, as a video playing.
        for (int i = 0; ms <= 4000 + StillKeeper::maxWaitMs; ms += 33, ++i)
            keeper.frame(screen(i % 2 ? Qt::blue : Qt::green, ms));
        QCOMPARE(kindsOf(keeper.stills()).mid(3), QList({AfterClick, AfterClick}));
        // One left waiting when the recording stops.
        run(Qt::cyan, ms + 50);
        keeper.press();
        run(Qt::yellow, ms + 50);
        keeper.finish();
        QCOMPARE(kindsOf(keeper.stills()).mid(3), QList({AfterClick, AfterClick, BeforeClick, AfterClick}));

        // Written out as they were.
        QCOMPARE(QFileInfo(keeper.stills()[0].path).fileName(), u"01-before-click.png"_s);
        QCOMPARE(QFileInfo(keeper.stills()[2].path).fileName(), u"03-kept.png"_s);
        QCOMPARE(QImage(keeper.stills()[0].path).pixelColor(1, 1), QColor(Qt::gray));
        QCOMPARE(QImage(keeper.stills()[1].path).pixelColor(1, 1), QColor(Qt::black));
        QCOMPARE(QImage(keeper.stills().last().path).pixelColor(1, 1), QColor(Qt::yellow));
        // Beside the recording, in its folder.
        QCOMPARE(ScreenRecorder::stillsDirectory(u"/a/Recording 1/Recording 1.mp4"_s),
                 QFileInfo(u"/a/Recording 1"_s).absoluteFilePath());
    }

    // Without a click: what the screen settles on after a change as big
    // as a tooltip, but not a blinking caret, once for a change that
    // animates, every few seconds for one that never stops, and nothing
    // of its own while a click waits.
    void keepsStillsOfChanges()
    {
        using enum Still::Kind;
        QTemporaryDir dir;
        StillKeeper keeper;
        keeper.setKeepChanges(true);
        keeper.begin(dir.filePath(u"rec stills"_s));
        QImage page(200, 100, QImage::Format_RGB32);
        page.fill(Qt::white);
        int ms = 0;
        const auto show = [&](const QImage &image, int forMs) {
            for (const int until = ms + forMs; ms < until; ms += 33) {
                StillKeeper::Frame frame;
                frame.image = image;
                frame.ms = ms;
                keeper.frame(frame);
            }
        };
        const auto with = [&](QRect rect, const QColor &color) {
            QImage image = page;
            QPainter(&image).fillRect(rect, color);
            return image;
        };

        // A caret blinking: nothing, even with a clock ticking far away.
        for (int i = 0; i < 6; ++i) {
            QImage image = i % 2 ? with(QRect(20, 20, 2, 14), Qt::black) : page;
            QPainter(&image).fillRect(QRect(180, 85 - i, 9, 1), Qt::black);
            show(image, 530);
        }
        QVERIFY(keeper.stills().isEmpty());
        show(page, 400);

        // A tooltip fading in, then shown: one still, once it is all there.
        const QRect tooltip(60, 40, 80, 24);
        show(with(tooltip, QColor(0xe0, 0xe0, 0xe0)), 66);
        show(with(tooltip, QColor(0x80, 0x80, 0x80)), 66);
        const int whole = ms;
        show(with(tooltip, Qt::black), 1000);
        QCOMPARE(kindsOf(keeper.stills()), QList({Change}));
        QVERIFY(keeper.stills()[0].ms >= whole + StillKeeper::changeSettleMs);
        QVERIFY(keeper.stills()[0].ms < whole + StillKeeper::changeSettleMs + 33);
        keeper.finish();
        QCOMPARE(QImage(keeper.stills()[0].path).pixelColor(tooltip.center()), QColor(Qt::black));

        // Gone again: the page as it was, a state of its own.
        show(page, 1000);
        QCOMPARE(kindsOf(keeper.stills()), QList({Change, Change}));

        // Never settling, as a video playing: one every few seconds.
        for (int i = 0; i < 270; ++i)
            show(with(QRect(0, 0, 100, 100), QColor::fromHsv(i, 255, 255)), 33);
        QCOMPARE(kindsOf(keeper.stills()).size(), 4);
        show(page, 1000);
        QCOMPARE(kindsOf(keeper.stills()).size(), 5);

        // A click: its own still after it, none for the change it makes;
        // the one before it is the page just kept.
        keeper.press();
        show(with(QRect(10, 10, 120, 60), Qt::green), 1000);
        QCOMPARE(kindsOf(keeper.stills()).mid(5), QList({AfterClick}));
        show(with(QRect(10, 10, 120, 60), Qt::green), 1000);
        QCOMPARE(kindsOf(keeper.stills()).size(), 6);
        QCOMPARE(QFileInfo(keeper.stills()[0].path).fileName(), u"01-change.png"_s);

        // Off: nothing without a click.
        StillKeeper quiet;
        quiet.begin(dir.filePath(u"quiet stills"_s));
        StillKeeper::Frame frame;
        frame.image = page;
        quiet.frame(frame);
        frame.image = with(tooltip, Qt::black);
        for (frame.ms = 33; frame.ms < 1000; frame.ms += 33)
            quiet.frame(frame);
        QVERIFY(quiet.stills().isEmpty());

        QCOMPARE(StillKeeper::changedPixels(page, with(QRect(5, 6, 7, 8), Qt::red)), 56);
        QCOMPARE(StillKeeper::changedPixels(page, page), 0);
    }

    // A menu walked through without clicking, as when showing a flow: one
    // still when it has opened, one for each item the pointer rests on,
    // none for one it only passes over, and one when it closes.
    void keepsStillsOfMenus()
    {
        QTemporaryDir dir;
        StillKeeper keeper;
        keeper.setKeepChanges(true);
        keeper.begin(dir.filePath(u"menu stills"_s));
        QImage page(640, 400, QImage::Format_RGB32);
        page.fill(QColor(0xf0, 0xf0, 0xf0));
        const QRect menu(100, 60, 180, 6 * 24);
        const auto menuWith = [&](qreal opacity, int hovered) {
            QImage image = page;
            QPainter p(&image);
            p.setOpacity(opacity);
            p.fillRect(menu, Qt::white);
            for (int i = 0; i < 6; ++i) {
                const QRect row(menu.x(), menu.y() + i * 24, menu.width(), 24);
                if (i == hovered)
                    p.fillRect(row, QColor(0x30, 0x8c, 0xe8));
                p.fillRect(row.adjusted(10, 8, -60, -8), i == hovered ? Qt::white : Qt::black);
            }
            return image;
        };
        int ms = 0;
        const auto show = [&](const QImage &image, int forMs) {
            for (const int until = ms + forMs; ms < until; ms += 16) {
                StillKeeper::Frame frame;
                frame.image = image;
                frame.ms = ms;
                keeper.frame(frame);
            }
        };
        show(page, 500);
        // Fading in over 150 ms, then shown.
        for (const qreal opacity : {0.2, 0.4, 0.6, 0.8})
            show(menuWith(opacity, -1), 40);
        show(menuWith(1, -1), 500);
        QCOMPARE(keeper.stills().size(), 1);
        // Rested on the first item, then a quarter second on the second.
        show(menuWith(1, 0), 450);
        show(menuWith(1, 1), 250);
        QCOMPARE(keeper.stills().size(), 3);
        // Passed over the third on the way to the fourth.
        show(menuWith(1, 2), 100);
        show(menuWith(1, 3), 450);
        QCOMPARE(keeper.stills().size(), 4);
        // Closed.
        show(page, 500);
        QCOMPARE(keeper.stills().size(), 5);
        keeper.finish();
        QCOMPARE(QImage(keeper.stills()[3].path).pixelColor(menu.x() + 2, menu.y() + 3 * 24 + 2),
                 QColor(0x30, 0x8c, 0xe8));
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

    void commandsSurviveNestedLoops()
    {
        // Opening the first editor spins an event loop while the sender of
        // "open" has hung up already.
        qputenv("GUCKSTE_INSTANCE", "test-" + QByteArray::number(QCoreApplication::applicationPid()));
        SingleInstance instance;
        QVERIFY(instance.claim());
        QVERIFY(instance.listen());
        QStringList received;
        connect(&instance, &SingleInstance::commandReceived, this, [&received](const QString &command) {
            received << command;
            QTest::qWait(200);
        });
        // Sent from a thread of its own, as another process would, waiting
        // for each to be taken: a socket of this thread is read only after
        // it has closed, and on Windows its unsent command closes with it.
        std::atomic<int> sent = 0;
        std::thread sender([&sent] {
            sent += SingleInstance::sendToRunning(u"open"_s);
            sent += SingleInstance::sendToRunning(u"still"_s);
        });
        auto joined = qScopeGuard([&sender] { sender.join(); });
        QTRY_COMPARE(received, (QStringList{u"open"_s, u"still"_s}));
        joined.dismiss();
        sender.join();
        QCOMPARE(sent, 2);
        instance.release();
        qunsetenv("GUCKSTE_INSTANCE");
    }

    // Black and white stripes, 4 px wide, for blurring to show.
    static QString writeStripes(const QString &path, int frames)
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
        recorder.setQuality(QMediaRecorder::VeryHighQuality);
        recorder.setOutputLocation(QUrl::fromLocalFile(path));
        recorder.record();
        QImage image(320, 240, QImage::Format_RGB32);
        for (int x = 0; x < 320; ++x)
            for (int y = 0; y < 240; ++y)
                image.setPixel(x, y, (x / 4) % 2 ? 0xffffffff : 0xff000000);
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

    // A video as a clip at the canvas's origin, as the editor opens one.
    static Annotation clipOf(const QString &path, int ms, QPointF at = {}, int startMs = 0)
    {
        static int next = 1;
        Annotation clip;
        clip.type = Annotation::Type::Video;
        clip.source = path;
        clip.sourceMs = ms;
        clip.startMs = startMs;
        clip.endMs = startMs + ms;
        clip.clip = next++;
        clip.image = QImage(320, 240, QImage::Format_ARGB32_Premultiplied);
        clip.image.fill(Qt::black);
        clip.points = {at, at + QPointF(320, 240)};
        return clip;
    }

    static VideoExporter::Job jobOf(const QString &target, const QList<Annotation> &layers)
    {
        VideoExporter::Job job;
        job.target = target;
        job.layers = layers;
        for (const Annotation &a : layers) {
            if (a.isPicture())
                job.canvas |= a.rect().toAlignedRect();
        }
        return job;
    }

    // The first frame after `afterUs` of a video, read back.
    static QImage frameOf(const QString &path, qint64 afterUs, qint64 *duration = nullptr)
    {
        QMediaPlayer player;
        QVideoSink sink;
        player.setVideoSink(&sink);
        QImage seen;
        QObject::connect(&sink, &QVideoSink::videoFrameChanged, &sink, [&seen, afterUs](const QVideoFrame &f) {
            if (seen.isNull() && f.isValid() && f.startTime() >= afterUs)
                seen = f.toImage();
        });
        player.setSource(QUrl::fromLocalFile(path));
        if (!QTest::qWaitFor([&player] { return player.mediaStatus() == QMediaPlayer::LoadedMedia; }, 10000))
            return {};
        if (duration)
            *duration = player.duration();
        player.play();
        if (!QTest::qWaitFor([&seen] { return !seen.isNull(); }, 10000))
            qWarning("No frame after %lld µs in %s", afterUs, qPrintable(path));
        player.stop();
        return seen;
    }

    // A second of a color with a tone, `hz` high, for mixing.
    static QString writeVideoWithSound(const QString &path, const QColor &color, int hz)
    {
        QMediaCaptureSession session;
        QVideoFrameInput video;
        QAudioBufferInput audio;
        QMediaRecorder recorder;
        session.setVideoFrameInput(&video);
        session.setAudioBufferInput(&audio);
        session.setRecorder(&recorder);
        recorder.setMediaFormat(Encoding::bestFormat());
        recorder.setVideoFrameRate(30);
        recorder.setVideoResolution(QSize(320, 240));
        recorder.setOutputLocation(QUrl::fromLocalFile(path));
        recorder.record();
        QImage image(320, 240, QImage::Format_RGB32);
        image.fill(color);
        QAudioFormat format;
        format.setSampleFormat(QAudioFormat::Float);
        format.setSampleRate(48000);
        format.setChannelCount(2);
        format.setChannelConfig(QAudioFormat::ChannelConfigStereo);
        // A tenth of a second of sound with every third frame.
        for (int i = 0; i < 30; ++i) {
            QVideoFrame frame = VideoFrames::toNV12(image);
            frame.setStartTime(i * 33333);
            frame.setEndTime((i + 1) * 33333);
            if (!QTest::qWaitFor([&video, &frame] { return video.sendVideoFrame(frame); }, 5000))
                return {};
            if (i % 3 == 0) {
                const int first = i / 3 * 4800;
                QByteArray data(4800 * 2 * sizeof(float), Qt::Uninitialized);
                auto *samples = reinterpret_cast<float *>(data.data());
                for (int k = 0; k < 4800; ++k)
                    samples[2 * k] = samples[2 * k + 1] = 0.5f * std::sin(2 * M_PI * hz * (first + k) / 48000.0);
                const QAudioBuffer buffer(data, format, qint64(first) * 1000000 / 48000);
                if (!QTest::qWaitFor([&audio, &buffer] { return audio.sendAudioBuffer(buffer); }, 5000))
                    return {};
            }
        }
        QTest::qWait(300);
        recorder.stop();
        if (!QTest::qWaitFor([&recorder] { return recorder.recorderState() == QMediaRecorder::StoppedState; }, 10000))
            return {};
        return recorder.actualLocation().toLocalFile();
    }

    // Clips with sound one after the other: the sound of each in its
    // time, all through the video.
    void exportsTheSoundOfClips()
    {
        QTemporaryDir dir;
        const QString first = writeVideoWithSound(dir.path() + u"/first.mp4"_s, Qt::blue, 440);
        const QString second = writeVideoWithSound(dir.path() + u"/second.mp4"_s, Qt::green, 880);
        QVERIFY(!first.isEmpty() && !second.isEmpty());
        VideoExporter exporter;
        QSignalSpy finished(&exporter, &VideoExporter::finished);
        QSignalSpy failed(&exporter, &VideoExporter::failed);
        const VideoExporter::Job job = jobOf(dir.path() + u"/out.mp4"_s,
                                             {clipOf(first, 1000), clipOf(second, 1000, {}, 1000)});
        QVERIFY(exporter.start(job));
        QTRY_VERIFY_WITH_TIMEOUT(finished.count() > 0 || failed.count() > 0, 30000);
        QVERIFY2(failed.isEmpty(), failed.isEmpty() ? "" : qPrintable(failed.first().first().toString()));

        // Played back: loud in both halves.
        QMediaPlayer player;
        QAudioBufferOutput out;
        player.setAudioBufferOutput(&out);
        qreal loudFirst = 0, loudSecond = 0;
        connect(&out, &QAudioBufferOutput::audioBufferReceived, this, [&](const QAudioBuffer &buffer) {
            qreal peak = 0;
            for (qsizetype i = 0; i < buffer.frameCount(); ++i)
                peak = qMax<qreal>(peak, std::abs(buffer.format().normalizedSampleValue(
                                             buffer.constData<char>() + i * buffer.format().bytesPerFrame())));
            (buffer.startTime() < 900000 ? loudFirst : loudSecond) = qMax(buffer.startTime() < 900000 ? loudFirst : loudSecond, peak);
        });
        player.setSource(QUrl::fromLocalFile(job.target));
        QTRY_COMPARE_WITH_TIMEOUT(player.mediaStatus(), QMediaPlayer::LoadedMedia, 10000);
        QVERIFY(player.hasAudio());
        QVERIFY2(player.duration() > 1800 && player.duration() < 2300, qPrintable(QString::number(player.duration())));
        player.play();
        QTRY_VERIFY_WITH_TIMEOUT(player.mediaStatus() == QMediaPlayer::EndOfMedia, 10000);
        QVERIFY2(loudFirst > 0.3, qPrintable(QString::number(loudFirst)));
        QVERIFY2(loudSecond > 0.3, qPrintable(QString::number(loudSecond)));
    }

    void exportsWithMarksAndTrim()
    {
        QTemporaryDir dir;
        const QString source = writeVideo(dir.path() + u"/source.mp4"_s, 90, Qt::blue);
        QVERIFY(QFileInfo(source).size() > 0);

        VideoExporter exporter;
        QSignalSpy finished(&exporter, &VideoExporter::finished);
        QSignalSpy failed(&exporter, &VideoExporter::failed);
        Annotation rect;
        rect.type = Annotation::Type::Rectangle;
        rect.points = {QPointF(40, 40), QPointF(200, 160)};
        rect.color = Qt::red;
        rect.size = 6;
        VideoExporter::Job job = jobOf(dir.path() + u"/out.mp4"_s, {clipOf(source, 3000), rect});
        job.startMs = 500;
        job.endMs = 1500;
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
        VideoExporter::Job copy = jobOf(dir.path() + u"/moved.mp4"_s, {clipOf(plain, 1000)});
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
        const VideoExporter::Job job = jobOf(dir.path() + u"/out.mp4"_s, {clipOf(source, 2000), early, late});
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

    // A pixelated area on a recording coarsens each frame under it.
    void exportsPixelation()
    {
        QTemporaryDir dir;
        const QString source = writeStripes(dir.path() + u"/stripes.mp4"_s, 30);
        QVERIFY(!source.isEmpty());
        VideoExporter exporter;
        QSignalSpy finished(&exporter, &VideoExporter::finished);
        QSignalSpy failed(&exporter, &VideoExporter::failed);
        Annotation blur;
        blur.type = Annotation::Type::Pixelate;
        blur.points = {QPointF(0, 0), QPointF(160, 240)};
        blur.size = 10;
        const VideoExporter::Job job = jobOf(dir.path() + u"/out.mp4"_s, {clipOf(source, 1000), blur});
        QVERIFY(exporter.start(job));
        QTRY_VERIFY_WITH_TIMEOUT(finished.count() > 0 || failed.count() > 0, 30000);
        QVERIFY2(failed.isEmpty(), failed.isEmpty() ? "" : qPrintable(failed.first().first().toString()));
        QMediaPlayer player;
        QVideoSink sink;
        player.setVideoSink(&sink);
        QImage seen;
        connect(&sink, &QVideoSink::videoFrameChanged, this, [&seen](const QVideoFrame &f) {
            if (seen.isNull() && f.isValid())
                seen = f.toImage();
        });
        player.setSource(QUrl::fromLocalFile(job.target));
        player.play();
        QTRY_VERIFY_WITH_TIMEOUT(!seen.isNull(), 10000);
        player.stop();
        // Stripes 1 px wide: sharp on the right, averaged to grey on the left.
        const auto spread = [&](int x0) {
            int low = 255, high = 0;
            for (int x = x0; x < x0 + 20; ++x) {
                const int g = qGray(seen.pixel(x, 120));
                low = qMin(low, g);
                high = qMax(high, g);
            }
            return high - low;
        };
        QVERIFY2(spread(220) > 100, qPrintable(QString::number(spread(220))));
        QVERIFY2(spread(60) < 60, qPrintable(QString::number(spread(60))));
    }

    // Two clips at once, side by side: each in its place on every frame.
    void exportsClipsSideBySide()
    {
        QTemporaryDir dir;
        const QString left = writeVideo(dir.path() + u"/left.mp4"_s, 30, Qt::blue);
        const QString right = writeVideo(dir.path() + u"/right.mp4"_s, 30, Qt::green);
        QVERIFY(!left.isEmpty() && !right.isEmpty());
        VideoExporter exporter;
        QSignalSpy finished(&exporter, &VideoExporter::finished);
        QSignalSpy failed(&exporter, &VideoExporter::failed);
        const VideoExporter::Job job = jobOf(dir.path() + u"/out.mp4"_s,
                                             {clipOf(left, 1000), clipOf(right, 1000, QPointF(320, 0))});
        QCOMPARE(job.canvas, QRect(0, 0, 640, 240));
        QVERIFY(exporter.start(job));
        QTRY_VERIFY_WITH_TIMEOUT(finished.count() > 0 || failed.count() > 0, 30000);
        QVERIFY2(failed.isEmpty(), failed.isEmpty() ? "" : qPrintable(failed.first().first().toString()));

        qint64 duration = 0;
        const QImage frame = frameOf(job.target, 300000, &duration);
        QVERIFY(!frame.isNull());
        QCOMPARE(frame.size(), QSize(640, 240));
        QVERIFY2(duration > 800 && duration < 1300, qPrintable(QString::number(duration)));
        const QColor a = frame.pixelColor(160, 120);
        const QColor b = frame.pixelColor(480, 120);
        QVERIFY2(a.blue() > 150 && a.green() < 100, qPrintable(a.name()));
        QVERIFY2(b.green() > 150 && b.blue() < 100, qPrintable(b.name()));
    }

    // Two clips one after the other: the second's frames follow the
    // first's, and the video is as long as both.
    void exportsClipsInSequence()
    {
        QTemporaryDir dir;
        const QString first = writeVideo(dir.path() + u"/first.mp4"_s, 30, Qt::blue);
        const QString second = writeVideo(dir.path() + u"/second.mp4"_s, 30, Qt::green);
        QVERIFY(!first.isEmpty() && !second.isEmpty());
        VideoExporter exporter;
        QSignalSpy finished(&exporter, &VideoExporter::finished);
        QSignalSpy failed(&exporter, &VideoExporter::failed);
        const VideoExporter::Job job = jobOf(dir.path() + u"/out.mp4"_s,
                                             {clipOf(first, 1000), clipOf(second, 1000, {}, 1000)});
        QVERIFY(exporter.start(job));
        QTRY_VERIFY_WITH_TIMEOUT(finished.count() > 0 || failed.count() > 0, 30000);
        QVERIFY2(failed.isEmpty(), failed.isEmpty() ? "" : qPrintable(failed.first().first().toString()));

        qint64 duration = 0;
        const QImage early = frameOf(job.target, 200000, &duration);
        const QImage late = frameOf(job.target, 1400000);
        QVERIFY2(duration > 1800 && duration < 2300, qPrintable(QString::number(duration)));
        QVERIFY(!early.isNull() && !late.isNull());
        const QColor a = early.pixelColor(160, 120);
        const QColor b = late.pixelColor(160, 120);
        QVERIFY2(a.blue() > 150 && a.green() < 100, qPrintable(a.name()));
        QVERIFY2(b.green() > 150 && b.blue() < 100, qPrintable(b.name()));
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
