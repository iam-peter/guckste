#include "VideoFrames.h"

#include <QVideoFrameFormat>

QVideoFrame VideoFrames::toNV12(const QImage &source)
{
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

QImage VideoFrames::toImage(const QVideoFrame &source, const QRect &rect)
{
    QVideoFrame frame = source;
    const QImage::Format imageFormat = QVideoFrameFormat::imageFormatFromPixelFormat(frame.pixelFormat());
    if (imageFormat != QImage::Format_Invalid && frame.map(QVideoFrame::ReadOnly)) {
        // Without converting: the frame's bytes read as an image, the
        // region copied out of it.
        const QImage whole(frame.bits(0), frame.width(), frame.height(), frame.bytesPerLine(0), imageFormat);
        const QImage image = whole.copy(rect.isNull() ? whole.rect() : rect & whole.rect());
        frame.unmap();
        return image;
    }
    const QImage image = frame.toImage();
    return rect.isNull() ? image : image.copy(rect);
}

QVideoFrame VideoFrames::cropped(const QVideoFrame &source, const QRect &rect)
{
    QVideoFrame out = toNV12(toImage(source, rect));
    out.setStartTime(source.startTime());
    out.setEndTime(source.endTime());
    return out;
}
