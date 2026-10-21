#pragma once

#include <QMediaFormat>
#include <QMediaRecorder>
#include <QSize>

// What the recorder and the exporter share: the video format this Qt can
// encode, and bit rates for the quality levels in the settings.
namespace Encoding {

// Low, Medium, High, VeryHigh: index into the settings' quality list.
enum Quality { Low, Medium, High, VeryHigh };

// Bits per second for a screen recording of that size and frame rate.
int bitRate(const QSize &size, double frameRate, int quality, QMediaFormat::VideoCodec codec);
// Sets the recorder to that bit rate.
void apply(QMediaRecorder &recorder, const QSize &size, double frameRate, int quality);

// The first of MP4/H.264, WebM/VP9, WebM/VP8, MP4/MPEG-4, MKV/MPEG-4 and
// AVI/MJPEG that encodes here: tried out once by encoding a few frames,
// since the codec table also lists hardware encoders that may not work.
QMediaFormat bestFormat();
// Encodes a few frames with the format; false if that fails.
bool canEncode(const QMediaFormat &format);
QString suffixFor(const QMediaFormat &format);

} // namespace Encoding
