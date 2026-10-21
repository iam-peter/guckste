#pragma once

#include <QImage>
#include <QRect>
#include <QVideoFrame>

// Frames as the encoder gets them: NV12, because of how Qt picks the
// encoder's format. From RGB frames it can land on a 12-bit format NVENC
// refuses for H.264; from NV12 it takes NV12, which every encoder has.
namespace VideoFrames {

// An image as an NV12 frame, BT.601 video range, even sizes.
QVideoFrame toNV12(const QImage &image);
// The frame's pixels as an image, the region only when `rect` is given.
QImage toImage(const QVideoFrame &frame, const QRect &rect = QRect());
// The frame cut down to `rect` (device pixels) as NV12, times kept.
QVideoFrame cropped(const QVideoFrame &frame, const QRect &rect);

} // namespace VideoFrames
