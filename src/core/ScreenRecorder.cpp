#include "ScreenRecorder.h"

#include "CursorCapture.h"
#include "Encoding.h"
#include "VideoFrames.h"

#include <QCursor>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QPainter>
#include <QMimeType>
#include <QTemporaryDir>
#include <QTimer>
#include <QScreen>
#include <QUrl>
#include <QVideoFrameFormat>

ScreenRecorder::ScreenRecorder(QObject *parent)
    : QObject(parent)
{
}

ScreenRecorder::Media &ScreenRecorder::media()
{
    if (m_media)
        return *m_media;
    m_media = std::make_unique<Media>();
    QMediaRecorder &recorder = m_media->recorder;
    m_media->captureSession.setVideoSink(&m_media->sink);
    connect(&m_media->sink, &QVideoSink::videoFrameChanged, this, &ScreenRecorder::onFrame);

    m_media->encodeSession.setVideoFrameInput(&m_media->input);
    m_media->encodeSession.setRecorder(&recorder);
    connect(&recorder, &QMediaRecorder::durationChanged, this, &ScreenRecorder::durationChanged);
    connect(&recorder, &QMediaRecorder::errorOccurred, this,
            [this](QMediaRecorder::Error, const QString &message) {
                // Can arrive from inside record(); stopping waits until
                // start() is through.
                m_failed = true;
                emit errorOccurred(message);
                QTimer::singleShot(0, this, &ScreenRecorder::stop);
            });
    connect(&recorder, &QMediaRecorder::recorderStateChanged, this, [this](QMediaRecorder::RecorderState state) {
        if (state == QMediaRecorder::StoppedState && m_state != Idle)
            finish();
    });
    return *m_media;
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
        media().captureSession.setScreenCapture(capture.get());
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
        media().captureSession.setScreenCapture(nullptr);
    }
    emit errorOccurred(lastError.isEmpty() ? tr("The screen cannot be captured") : lastError);
    return false;
}

bool ScreenRecorder::start(const QRect &region, const QString &path, const Options &options)
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
    m_options = options;
    m_firstFrame = QImage();
    m_keys.clear();
    emit recentKeysChanged();
    m_stills.setKeepChanges(options.stillsOnChanges, screen ? screen->devicePixelRatio() : 1);
    m_stills.begin(stillsDirectory(path));
    emit stillsChanged();

    if (!startCapture(screen))
        return false;

    m_stillsMs = 0;
    if (options.microphone && options.video) {
        m_audio = std::make_unique<QAudioInput>();
        media().encodeSession.setAudioInput(m_audio.get());
    } else {
        media().encodeSession.setAudioInput(nullptr);
        m_audio.reset();
    }
    if ((options.showKeys || options.stillsOnClicks) && KeyListener::supported()) {
        m_keyListener = std::make_unique<KeyListener>();
        if (options.showKeys) {
            connect(m_keyListener.get(), &KeyListener::keyPressed, this, [this](const QString &label) {
                m_keys.add(label);
                emit recentKeysChanged();
            });
        }
        m_keyListener->setListenToButtons(options.stillsOnClicks);
        connect(m_keyListener.get(), &KeyListener::buttonPressed, this, &ScreenRecorder::onButtonPressed);
        if (!m_keyListener->begin()) {
            m_keyListener.reset();
            const QString why = KeyListener::requirement();
            emit notice(why.isEmpty() ? tr("The keys and clicks cannot be listened to here.") : why);
        }
    }
    emit outputPathChanged();
    QMediaFormat format = Encoding::bestFormat();
    if (!options.microphone)
        format.setAudioCodec(QMediaFormat::AudioCodec::Unspecified);
    media().recorder.setMediaFormat(format);
    media().recorder.setVideoFrameRate(options.frameRate);
    const QSize size(qRound(m_region.width() * m_devicePixelRatio) & ~1,
                     qRound(m_region.height() * m_devicePixelRatio) & ~1);
    media().recorder.setVideoResolution(size);
    Encoding::apply(media().recorder, size, options.frameRate, options.quality);
    media().recorder.setOutputLocation(QUrl::fromLocalFile(path));
    if (!options.video)
        QDir().mkpath(QFileInfo(path).absolutePath());

    setState(Recording);
    m_startTime = -1;
    m_starting = true;
    emit startingChanged();
    emit outputPathChanged();
    QTimer::singleShot(warmUpMs, this, &ScreenRecorder::beginEncoding);
    return true;
}

void ScreenRecorder::beginEncoding()
{
    if (!m_starting)
        return;
    m_starting = false;
    emit startingChanged();
    // canPause() changes with it.
    emit outputPathChanged();
    if (m_state != Recording || !m_options.video)
        return;
    media().recorder.record();
    if (m_failed)
        stop();
}

void ScreenRecorder::onFrame(const QVideoFrame &frame)
{
    if (m_state != Recording || m_starting || !frame.isValid())
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
    // Cropped, the pointer and the keys painted in, and converted: see
    // VideoFrames.
    QImage image = VideoFrames::toImage(frame, m_cropDevice);
    if (image.format() != QImage::Format_RGB32 && image.format() != QImage::Format_ARGB32_Premultiplied)
        image = image.convertToFormat(QImage::Format_RGB32);
    const CursorCapture::Cursor cursor = m_options.includeCursor ? CursorCapture::current() : CursorCapture::Cursor();
    if (m_startTime < 0)
        m_startTime = qMax<qint64>(0, frame.startTime());
    {
        // As it is before anything is painted in, and as big as the
        // video, which has even sizes.
        StillKeeper::Frame still;
        const bool even = image.width() % 2 == 0 && image.height() % 2 == 0;
        still.image = even ? image : image.copy(0, 0, image.width() & ~1, image.height() & ~1);
        const qint64 us = frame.startTime() >= 0 ? frame.startTime() - m_startTime - m_pausedTotal : 0;
        // Rounded up: a frame shows from its start on.
        still.ms = int((qMax<qint64>(0, us) + 999) / 1000);
        if (cursor.ok && !cursor.image.isNull()) {
            const qreal scale = m_devicePixelRatio / cursor.imageDevicePixelRatio;
            const QPointF at = QPointF(cursor.position - m_region.topLeft()) * m_devicePixelRatio
                               - QPointF(cursor.hotspot) * scale;
            still.pointer = cursor.image;
            still.pointerRect = QRectF(at, QSizeF(cursor.image.size()) * scale);
        }
        const auto before = m_stills.stills().size();
        m_stills.frame(still);
        if (m_stills.stills().size() != before)
            emit stillsChanged();
        if (!m_options.video) {
            if (still.ms / 1000 != m_stillsMs / 1000)
                emit durationChanged();
            m_stillsMs = still.ms;
            return;
        }
    }
    if (m_options.includeCursor || !m_keys.isEmpty()) {
        if (m_options.includeCursor)
            CursorCapture::paint(image, m_region, m_devicePixelRatio, cursor);
        if (!m_keys.isEmpty()) {
            QPainter painter(&image);
            m_keys.paint(painter, image.rect());
        }
    }
    if (m_firstFrame.isNull())
        m_firstFrame = image.convertToFormat(QImage::Format_RGB32);
    QVideoFrame out = VideoFrames::toNV12(image);
    if (!out.isValid())
        return;
    out.setStartTime(frame.startTime() >= 0 ? frame.startTime() - m_startTime : -1);
    out.setEndTime(frame.endTime() >= 0 ? frame.endTime() - m_startTime : -1);
    if (m_pausedTotal > 0) {
        // Frames are explicitly shared, so this changes the sink's frame
        // too, which nothing else reads.
        if (out.startTime() >= 0)
            out.setStartTime(out.startTime() - m_pausedTotal);
        if (out.endTime() >= 0)
            out.setEndTime(out.endTime() - m_pausedTotal);
    }
    media().input.sendVideoFrame(out);
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
    if (m_starting) {
        m_starting = false;
        emit startingChanged();
    }
    setState(Stopping);
    if (m_screenCapture)
        m_screenCapture->stop();
    if (media().recorder.recorderState() == QMediaRecorder::StoppedState)
        finish();
    else
        media().recorder.stop();
}

void ScreenRecorder::keepStill()
{
    if (m_state != Recording || m_starting)
        return;
    m_stills.keep();
    emit stillsChanged();
}

void ScreenRecorder::onButtonPressed()
{
    // Clicks on anything outside, like the recording's own panel, are not
    // part of it.
    if (m_state != Recording || m_starting || !m_region.contains(QCursor::pos()))
        return;
    const auto before = m_stills.stills().size();
    m_stills.press();
    if (m_stills.stills().size() != before)
        emit stillsChanged();
}

QString ScreenRecorder::stillsDirectory(const QString &recording)
{
    return QFileInfo(recording).absolutePath();
}

QStringList ScreenRecorder::recentKeys() const
{
    return m_keys.labels();
}

void ScreenRecorder::finish()
{
    media().captureSession.setScreenCapture(nullptr);
    m_screenCapture.reset();
    if (m_keyListener) {
        m_keyListener->end();
        m_keyListener.reset();
    }
    media().encodeSession.setAudioInput(nullptr);
    m_audio.reset();
    m_stills.finish();
    emit stillsChanged();
    setState(Idle);
    if (!m_options.video) {
        if (m_stills.stills().isEmpty()) {
            QDir().rmdir(m_stills.directory());
            emit errorOccurred(tr("No stills were kept"));
            return;
        }
        emit stillsFinished(m_stills.directory());
        return;
    }
    const QUrl location = media().recorder.actualLocation();
    const QString path = location.isLocalFile() ? location.toLocalFile() : m_outputPath;
    if (m_failed || QFileInfo(path).size() == 0) {
        // No video worth keeping; the stills are whole all the same.
        QFile::remove(path);
        if (!m_stills.stills().isEmpty()) {
            emit stillsFinished(m_stills.directory());
            return;
        }
        QDir().rmdir(m_stills.directory());
        if (!m_failed)
            emit errorOccurred(tr("Nothing was recorded"));
        return;
    }
    emit finished(path);
}
