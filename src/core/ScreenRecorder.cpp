#include "ScreenRecorder.h"

#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMimeType>
#include <QTemporaryDir>
#include <QTimer>
#include <QScreen>
#include <QUrl>
#include <QVideoFrameFormat>

ScreenRecorder::ScreenRecorder(QObject *parent)
    : QObject(parent)
{
    m_captureSession.setVideoSink(&m_sink);
    connect(&m_sink, &QVideoSink::videoFrameChanged, this, &ScreenRecorder::onFrame);

    m_encodeSession.setVideoFrameInput(&m_input);
    m_encodeSession.setRecorder(&m_recorder);
    connect(&m_recorder, &QMediaRecorder::durationChanged, this, &ScreenRecorder::durationChanged);
    connect(&m_recorder, &QMediaRecorder::errorOccurred, this,
            [this](QMediaRecorder::Error, const QString &message) {
                // Can arrive from inside record(); stopping waits until
                // start() is through.
                m_failed = true;
                emit errorOccurred(message);
                QTimer::singleShot(0, this, &ScreenRecorder::stop);
            });
    connect(&m_recorder, &QMediaRecorder::recorderStateChanged, this, [this](QMediaRecorder::RecorderState state) {
        if (state == QMediaRecorder::StoppedState && m_state != Idle)
            finish();
    });
}

ScreenRecorder::~ScreenRecorder()
{
    if (m_screenCapture)
        m_screenCapture->stop();
}

void ScreenRecorder::setState(State state)
{
    if (m_state == state)
        return;
    m_state = state;
    emit stateChanged();
}

bool ScreenRecorder::canEncode(const QMediaFormat &format)
{
    if (!format.isSupported(QMediaFormat::Encode))
        return false;
    QTemporaryDir dir;
    QMediaCaptureSession session;
    QVideoFrameInput input;
    QMediaRecorder recorder;
    session.setVideoFrameInput(&input);
    session.setRecorder(&recorder);
    recorder.setMediaFormat(format);
    recorder.setVideoFrameRate(30);
    recorder.setVideoResolution(QSize(256, 144));
    const QString path = dir.path() + QStringLiteral("/probe.") + suffixFor(format);
    recorder.setOutputLocation(QUrl::fromLocalFile(path));
    bool failed = false;
    connect(&recorder, &QMediaRecorder::errorOccurred, &recorder, [&failed] { failed = true; });
    recorder.record();
    QImage image(256, 144, QImage::Format_RGB32);
    image.fill(Qt::darkGray);
    for (int i = 0; i < 5 && !failed; ++i) {
        // As the recording will send them.
        QVideoFrame frame = toNV12(image);
        frame.setStartTime(i * 33333);
        frame.setEndTime((i + 1) * 33333);
        input.sendVideoFrame(frame);
        QCoreApplication::processEvents();
    }
    QEventLoop loop;
    connect(&recorder, &QMediaRecorder::recorderStateChanged, &loop, [&loop](QMediaRecorder::RecorderState s) {
        if (s == QMediaRecorder::StoppedState)
            loop.quit();
    });
    QTimer::singleShot(100, &recorder, &QMediaRecorder::stop);
    QTimer::singleShot(3000, &loop, &QEventLoop::quit);
    if (recorder.recorderState() != QMediaRecorder::StoppedState)
        loop.exec();
    return !failed && QFileInfo(path).size() > 0;
}

QMediaFormat ScreenRecorder::bestFormat()
{
    static QMediaFormat chosen;
    static bool probed = false;
    if (probed)
        return chosen;
    struct Candidate
    {
        QMediaFormat::FileFormat file;
        QMediaFormat::VideoCodec video;
        QMediaFormat::AudioCodec audio;
    };
    const Candidate candidates[] = {
        {QMediaFormat::MPEG4, QMediaFormat::VideoCodec::H264, QMediaFormat::AudioCodec::AAC},
        {QMediaFormat::WebM, QMediaFormat::VideoCodec::VP9, QMediaFormat::AudioCodec::Opus},
        {QMediaFormat::WebM, QMediaFormat::VideoCodec::VP8, QMediaFormat::AudioCodec::Opus},
        {QMediaFormat::MPEG4, QMediaFormat::VideoCodec::MPEG4, QMediaFormat::AudioCodec::AAC},
        {QMediaFormat::Matroska, QMediaFormat::VideoCodec::MPEG4, QMediaFormat::AudioCodec::AAC},
        {QMediaFormat::AVI, QMediaFormat::VideoCodec::MotionJPEG, QMediaFormat::AudioCodec::MP3},
    };
    for (const Candidate &c : candidates) {
        QMediaFormat format(c.file);
        format.setVideoCodec(c.video);
        if (!canEncode(format))
            continue;
        format.setAudioCodec(c.audio);
        if (!format.isSupported(QMediaFormat::Encode))
            format.setAudioCodec(QMediaFormat::AudioCodec::Unspecified);
        chosen = format;
        probed = true;
        qInfo("Recording as %s with %s", qPrintable(format.mimeType().name()),
              qPrintable(QMediaFormat::videoCodecName(format.videoCodec())));
        return chosen;
    }
    // Whatever this Qt has.
    chosen = QMediaFormat();
    chosen.resolveForEncoding(QMediaFormat::RequiresVideo);
    probed = true;
    return chosen;
}

QString ScreenRecorder::suffixFor(const QMediaFormat &format)
{
    const QString suffix = format.mimeType().preferredSuffix();
    return suffix.isEmpty() ? QStringLiteral("mp4") : suffix;
}

QVideoFrame ScreenRecorder::toNV12(const QImage &source)
{
    // Even sizes, as 4:2:0 wants them.
    const QImage image = source.convertToFormat(QImage::Format_RGB32);
    const int width = image.width() & ~1;
    const int height = image.height() & ~1;
    if (width < 2 || height < 2)
        return {};
    QVideoFrameFormat format(QSize(width, height), QVideoFrameFormat::Format_NV12);
    format.setColorSpace(QVideoFrameFormat::ColorSpace_BT601);
    format.setColorRange(QVideoFrameFormat::ColorRange_Video);
    QVideoFrame frame(format);
    if (!frame.map(QVideoFrame::WriteOnly))
        return {};
    uchar *yPlane = frame.bits(0);
    uchar *uvPlane = frame.bits(1);
    const int yStride = frame.bytesPerLine(0);
    const int uvStride = frame.bytesPerLine(1);
    for (int y = 0; y < height; y += 2) {
        const auto *row0 = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        const auto *row1 = reinterpret_cast<const QRgb *>(image.constScanLine(y + 1));
        uchar *y0 = yPlane + y * yStride;
        uchar *y1 = y0 + yStride;
        uchar *uv = uvPlane + (y / 2) * uvStride;
        for (int x = 0; x < width; x += 2) {
            int sumU = 0, sumV = 0;
            const QRgb pixels[4] = {row0[x], row0[x + 1], row1[x], row1[x + 1]};
            uchar *targets[4] = {y0 + x, y0 + x + 1, y1 + x, y1 + x + 1};
            for (int i = 0; i < 4; ++i) {
                const int r = qRed(pixels[i]), g = qGreen(pixels[i]), b = qBlue(pixels[i]);
                // BT.601, video range: Y 16..235, U and V 16..240.
                *targets[i] = uchar((66 * r + 129 * g + 25 * b + 128 + (16 << 8)) >> 8);
                sumU += (-38 * r - 74 * g + 112 * b + 128) >> 8;
                sumV += (112 * r - 94 * g - 18 * b + 128) >> 8;
            }
            uv[x] = uchar(qBound(0, 128 + sumU / 4, 255));
            uv[x + 1] = uchar(qBound(0, 128 + sumV / 4, 255));
        }
    }
    frame.unmap();
    return frame;
}

QVideoFrame ScreenRecorder::cropped(const QVideoFrame &source, const QRect &rect)
{
    QVideoFrame frame = source;
    QImage image;
    const QImage::Format imageFormat = QVideoFrameFormat::imageFormatFromPixelFormat(frame.pixelFormat());
    if (imageFormat != QImage::Format_Invalid && frame.map(QVideoFrame::ReadOnly)) {
        // Without converting: the frame's bytes read as an image, the
        // region copied out of it.
        const QImage whole(frame.bits(0), frame.width(), frame.height(), frame.bytesPerLine(0), imageFormat);
        image = whole.copy(rect & whole.rect());
        frame.unmap();
    } else {
        image = frame.toImage().copy(rect);
    }
    QVideoFrame out = toNV12(image);
    out.setStartTime(source.startTime());
    out.setEndTime(source.endTime());
    return out;
}

bool ScreenRecorder::startCapture(QScreen *preferred)
{
    // Qt's X11 capture can only open the first monitor, and then delivers
    // the whole desktop; so when the screen under the region fails, the
    // others are tried, and the frames tell what they cover.
    QList<QScreen *> screens = QGuiApplication::screens();
    screens.removeAll(preferred);
    screens.prepend(preferred);
    QString lastError;
    for (QScreen *screen : std::as_const(screens)) {
        auto capture = std::make_unique<QScreenCapture>();
        capture->setScreen(screen);
        m_captureSession.setScreenCapture(capture.get());
        capture->start();
        if (capture->error() == QScreenCapture::NoError && capture->isActive()) {
            m_screenCapture = std::move(capture);
            m_screenGeometry = screen->geometry();
            m_devicePixelRatio = screen->devicePixelRatio();
            connect(m_screenCapture.get(), &QScreenCapture::errorOccurred, this,
                    [this](QScreenCapture::Error, const QString &message) {
                        m_failed = true;
                        emit errorOccurred(message);
                        QTimer::singleShot(0, this, &ScreenRecorder::stop);
                    });
            return true;
        }
        lastError = capture->errorString();
        m_captureSession.setScreenCapture(nullptr);
    }
    emit errorOccurred(lastError.isEmpty() ? tr("The screen cannot be captured") : lastError);
    return false;
}

bool ScreenRecorder::start(const QRect &region, const QString &path, int frameRate, bool microphone)
{
    if (m_state != Idle) {
        emit errorOccurred(tr("Already recording"));
        return false;
    }
    QRect desktop;
    for (QScreen *screen : QGuiApplication::screens())
        desktop |= screen->geometry();
    m_region = region & desktop;
    if (m_region.isEmpty()) {
        emit errorOccurred(tr("Nothing to record"));
        return false;
    }
    m_virtualGeometry = desktop;
    QScreen *screen = QGuiApplication::screenAt(m_region.center());
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    m_wholeScreen = m_region == desktop || (screen && m_region == screen->geometry());
    m_cropKnown = false;
    emit regionChanged();

    m_outputPath = path;
    m_failed = false;
    m_pausedTotal = 0;

    if (!startCapture(screen))
        return false;

    if (microphone) {
        m_audio = std::make_unique<QAudioInput>();
        m_encodeSession.setAudioInput(m_audio.get());
    } else {
        m_encodeSession.setAudioInput(nullptr);
        m_audio.reset();
    }
    emit outputPathChanged();
    QMediaFormat format = bestFormat();
    if (!microphone)
        format.setAudioCodec(QMediaFormat::AudioCodec::Unspecified);
    m_recorder.setMediaFormat(format);
    m_recorder.setQuality(QMediaRecorder::HighQuality);
    m_recorder.setVideoFrameRate(frameRate);
    m_recorder.setVideoResolution(QSize(qRound(m_region.width() * m_devicePixelRatio),
                                        qRound(m_region.height() * m_devicePixelRatio)));
    m_recorder.setOutputLocation(QUrl::fromLocalFile(path));

    setState(Recording);
    m_recorder.record();
    if (m_failed) {
        stop();
        return false;
    }
    return true;
}

void ScreenRecorder::onFrame(const QVideoFrame &frame)
{
    if (m_state != Recording || !frame.isValid())
        return;
    if (!m_cropKnown) {
        // The frame is the screen, or the whole desktop on X11.
        const QSize desktopSize = m_virtualGeometry.size() * m_devicePixelRatio;
        const QPoint origin = frame.size() == desktopSize && m_virtualGeometry != m_screenGeometry
                                  ? m_virtualGeometry.topLeft()
                                  : m_screenGeometry.topLeft();
        const QRect local = m_region.translated(-origin);
        m_cropDevice = QRect(qRound(local.x() * m_devicePixelRatio), qRound(local.y() * m_devicePixelRatio),
                             qRound(local.width() * m_devicePixelRatio), qRound(local.height() * m_devicePixelRatio));
        m_cropKnown = true;
    }
    // Cropped, and always converted: see cropped().
    QVideoFrame out = cropped(frame, m_cropDevice);
    if (!out.isValid())
        return;
    if (m_pausedTotal > 0) {
        // Frames are explicitly shared, so this changes the sink's frame
        // too, which nothing else reads.
        if (out.startTime() >= 0)
            out.setStartTime(out.startTime() - m_pausedTotal);
        if (out.endTime() >= 0)
            out.setEndTime(out.endTime() - m_pausedTotal);
    }
    m_input.sendVideoFrame(out);
}

void ScreenRecorder::pause()
{
    if (m_state != Recording || !canPause())
        return;
    // Frames are dropped while paused, and the ones after are moved back
    // by the pause, so the video simply goes on.
    m_pauseTimer.start();
    setState(Paused);
}

void ScreenRecorder::resume()
{
    if (m_state != Paused)
        return;
    m_pausedTotal += m_pauseTimer.nsecsElapsed() / 1000;
    setState(Recording);
}

void ScreenRecorder::stop()
{
    if (m_state == Idle || m_state == Stopping)
        return;
    setState(Stopping);
    if (m_screenCapture)
        m_screenCapture->stop();
    if (m_recorder.recorderState() == QMediaRecorder::StoppedState)
        finish();
    else
        m_recorder.stop();
}

void ScreenRecorder::finish()
{
    m_captureSession.setScreenCapture(nullptr);
    m_screenCapture.reset();
    m_encodeSession.setAudioInput(nullptr);
    m_audio.reset();
    const QString path = m_recorder.actualLocation().isLocalFile() ? m_recorder.actualLocation().toLocalFile()
                                                                    : m_outputPath;
    setState(Idle);
    if (m_failed || QFileInfo(path).size() == 0) {
        // Nothing worth keeping.
        QFile::remove(path);
        if (!m_failed)
            emit errorOccurred(tr("Nothing was recorded"));
        return;
    }
    emit finished(path);
}
