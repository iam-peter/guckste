#include "VideoFrames.h"

#include <QVideoFrameFormat>

#include <optional>
#include <utility>

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

namespace {

// How a YUV format lays out its planes.
struct YuvLayout
{
    // Chroma halved across, and down.
    bool halfWidth = true;
    bool halfHeight = true;
    // U and V interleaved in plane 1, V first when `swapped`.
    bool interleaved = false;
    bool swapped = false;
    // Two bytes a sample; `shift` brings one down to eight bits.
    bool wide = false;
    int shift = 0;
};

std::optional<YuvLayout> layoutOf(QVideoFrameFormat::PixelFormat format)
{
    switch (format) {
    case QVideoFrameFormat::Format_NV12: return YuvLayout{true, true, true, false, false, 0};
    case QVideoFrameFormat::Format_NV21: return YuvLayout{true, true, true, true, false, 0};
    case QVideoFrameFormat::Format_YUV420P: return YuvLayout{true, true, false, false, false, 0};
    case QVideoFrameFormat::Format_YV12: return YuvLayout{true, true, false, true, false, 0};
    case QVideoFrameFormat::Format_YUV422P: return YuvLayout{true, false, false, false, false, 0};
    case QVideoFrameFormat::Format_P010:
    case QVideoFrameFormat::Format_P016: return YuvLayout{true, true, true, false, true, 8};
    case QVideoFrameFormat::Format_YUV420P10: return YuvLayout{true, true, false, false, true, 2};
    default: return std::nullopt;
    }
}

QImage yuvToImage(const QVideoFrame &frame, const YuvLayout &layout, const QRect &rect)
{
    const QVideoFrameFormat format = frame.surfaceFormat();
    const bool full = format.colorRange() == QVideoFrameFormat::ColorRange_Full;
    QVideoFrameFormat::ColorSpace space = format.colorSpace();
    if (space == QVideoFrameFormat::ColorSpace_Undefined)
        space = frame.height() >= 720 ? QVideoFrameFormat::ColorSpace_BT709 : QVideoFrameFormat::ColorSpace_BT601;
    const bool bt709 = space != QVideoFrameFormat::ColorSpace_BT601;
    // R = Y + rv·V, G = Y − gu·U − gv·V, B = Y + bu·U, in 1/65536,
    // video range stretched to full.
    const double yScale = full ? 1.0 : 255.0 / 219.0;
    const double cScale = full ? 1.0 : 255.0 / 224.0;
    const double kr = bt709 ? 0.2126 : 0.299, kb = bt709 ? 0.0722 : 0.114, kg = 1 - kr - kb;
    const int ys = qRound(yScale * 65536);
    const int rv = qRound(2 * (1 - kr) * cScale * 65536);
    const int bu = qRound(2 * (1 - kb) * cScale * 65536);
    const int gu = qRound(2 * (1 - kb) * kb / kg * cScale * 65536);
    const int gv = qRound(2 * (1 - kr) * kr / kg * cScale * 65536);
    const int yOffset = full ? 0 : 16;

    const QRect area = rect & QRect(0, 0, frame.width(), frame.height());
    QImage image(area.size(), QImage::Format_RGB32);
    if (image.isNull())
        return image;
    const uchar *yPlane = frame.bits(0);
    const uchar *uPlane = frame.bits(1);
    const uchar *vPlane = layout.interleaved ? frame.bits(1) : frame.bits(2);
    // YV12 has V before U; the interleaved ones say so per sample below.
    if (layout.swapped && !layout.interleaved)
        std::swap(uPlane, vPlane);
    const int yStride = frame.bytesPerLine(0);
    const int cStride = frame.bytesPerLine(1);
    const auto sample = [&](const uchar *row, int x) -> int {
        if (!layout.wide)
            return row[x];
        return reinterpret_cast<const quint16 *>(row)[x] >> layout.shift;
    };
    for (int y = 0; y < area.height(); ++y) {
        const int sy = area.y() + y;
        const int cy = layout.halfHeight ? sy / 2 : sy;
        const uchar *yRow = yPlane + qsizetype(sy) * yStride;
        const uchar *uRow = uPlane + qsizetype(cy) * cStride;
        const uchar *vRow = vPlane + qsizetype(cy) * cStride;
        auto *out = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < area.width(); ++x) {
            const int sx = area.x() + x;
            const int cx = layout.halfWidth ? sx / 2 : sx;
            const int luma = (sample(yRow, sx) - yOffset) * ys;
            int u, v;
            if (layout.interleaved) {
                // U then V, side by side; for the swapped ones V first.
                const int first = sample(uRow, cx * 2);
                const int second = sample(uRow, cx * 2 + 1);
                u = (layout.swapped ? second : first) - 128;
                v = (layout.swapped ? first : second) - 128;
            } else {
                u = sample(uRow, cx) - 128;
                v = sample(vRow, cx) - 128;
            }
            const int r = (luma + rv * v + 32768) >> 16;
            const int g = (luma - gu * u - gv * v + 32768) >> 16;
            const int b = (luma + bu * u + 32768) >> 16;
            out[x] = qRgb(qBound(0, r, 255), qBound(0, g, 255), qBound(0, b, 255));
        }
    }
    return image;
}

} // namespace

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
    if (const auto layout = layoutOf(frame.pixelFormat()); layout && frame.map(QVideoFrame::ReadOnly)) {
        const QImage image = yuvToImage(frame, *layout, rect.isNull() ? QRect(0, 0, frame.width(), frame.height()) : rect);
        frame.unmap();
        if (!image.isNull())
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
