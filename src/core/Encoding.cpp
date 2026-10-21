#include "Encoding.h"

#include "VideoFrames.h"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QImage>
#include <QMediaCaptureSession>
#include <QMimeType>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <QVideoFrameInput>
#include <QtMath>

int Encoding::bitRate(const QSize &size, double frameRate, int quality, QMediaFormat::VideoCodec codec)
{
    // Bits per pixel and frame at 30 fps for H.264 on screen content, where
    // most of the picture stands still; doubling the frame rate costs
    // half as much again.
    static const double bitsPerPixel[] = {1.0, 2.0, 4.0, 8.0};
    const int level = qBound(0, quality, 3);
    double bits = bitsPerPixel[level] * size.width() * size.height();
    const double fps = frameRate > 0 ? frameRate : 30;
    bits *= std::pow(1.5, std::log2(fps / 30.0));
    if (codec == QMediaFormat::VideoCodec::MPEG4)
        bits *= 1.5;
    else if (codec == QMediaFormat::VideoCodec::MotionJPEG)
        bits *= 8;
    return int(qBound(200000.0, bits, 200000000.0));
}

void Encoding::apply(QMediaRecorder &recorder, const QSize &size, double frameRate, int quality)
{
    recorder.setQuality(QMediaRecorder::HighQuality);
    recorder.setEncodingMode(QMediaRecorder::AverageBitRateEncoding);
    recorder.setVideoBitRate(bitRate(size, frameRate, quality, recorder.mediaFormat().videoCodec()));
    recorder.setAudioBitRate(128000);
}

QString Encoding::suffixFor(const QMediaFormat &format)
{
    const QString suffix = format.mimeType().preferredSuffix();
    return suffix.isEmpty() ? QStringLiteral("mp4") : suffix;
}

bool Encoding::canEncode(const QMediaFormat &format)
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
    // Big enough for every hardware encoder.
    recorder.setVideoResolution(QSize(256, 144));
    const QString path = dir.path() + QStringLiteral("/probe.") + suffixFor(format);
    recorder.setOutputLocation(QUrl::fromLocalFile(path));
    bool failed = false;
    QObject::connect(&recorder, &QMediaRecorder::errorOccurred, &recorder, [&failed] { failed = true; });
    recorder.record();
    QImage image(256, 144, QImage::Format_RGB32);
    image.fill(Qt::darkGray);
    for (int i = 0; i < 5 && !failed; ++i) {
        // As a recording sends them.
        QVideoFrame frame = VideoFrames::toNV12(image);
        frame.setStartTime(i * 33333);
        frame.setEndTime((i + 1) * 33333);
        QDeadlineTimer deadline(500);
        while (!input.sendVideoFrame(frame) && !deadline.hasExpired() && !failed)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    QEventLoop loop;
    QObject::connect(&recorder, &QMediaRecorder::recorderStateChanged, &loop,
                     [&loop](QMediaRecorder::RecorderState s) {
                         if (s == QMediaRecorder::StoppedState)
                             loop.quit();
                     });
    QTimer::singleShot(100, &recorder, &QMediaRecorder::stop);
    QTimer::singleShot(3000, &loop, &QEventLoop::quit);
    if (recorder.recorderState() != QMediaRecorder::StoppedState)
        loop.exec();
    return !failed && QFileInfo(path).size() > 0;
}

QMediaFormat Encoding::bestFormat()
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
