#include "ScreenRecorder.h"

#include "Encoding.h"
#include "VideoFrames.h"

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

bool ScreenRecorder::start(const QRect &region, const QString &path, int frameRate, int quality, bool microphone)
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
    m_quality = quality;
    m_firstFrame = QImage();

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
    QMediaFormat format = Encoding::bestFormat();
    if (!microphone)
        format.setAudioCodec(QMediaFormat::AudioCodec::Unspecified);
    m_recorder.setMediaFormat(format);
    m_recorder.setVideoFrameRate(frameRate);
    const QSize size(qRound(m_region.width() * m_devicePixelRatio) & ~1,
                     qRound(m_region.height() * m_devicePixelRatio) & ~1);
    m_recorder.setVideoResolution(size);
    Encoding::apply(m_recorder, size, frameRate, quality);
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
    // Cropped, and always converted: see VideoFrames.
    const QImage image = VideoFrames::toImage(frame, m_cropDevice);
    if (m_firstFrame.isNull())
        m_firstFrame = image.convertToFormat(QImage::Format_RGB32);
    QVideoFrame out = VideoFrames::toNV12(image);
    if (!out.isValid())
        return;
    out.setStartTime(frame.startTime());
    out.setEndTime(frame.endTime());
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
