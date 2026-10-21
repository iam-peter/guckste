#include "CursorCapture.h"

#include <QCursor>
#include <QGuiApplication>
#include <QScreen>
#include <QPainter>
#include <QPainterPath>

#if defined(Q_OS_WIN)
#include <windows.h>
#elif defined(Q_OS_MACOS)
bool macCursor(CursorCapture::Cursor &cursor);
#elif defined(GUCKSTE_HAVE_X11)
#include <X11/Xlib.h>
#include <X11/extensions/Xfixes.h>
#endif

namespace {

// A plain arrow for where the real one cannot be had.
QImage fallbackArrow()
{
    static const QImage arrow = [] {
        QImage image(24, 32, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter p(&image);
        p.setRenderHint(QPainter::Antialiasing);
        QPainterPath path;
        path.moveTo(1, 1);
        path.lineTo(1, 23);
        path.lineTo(7, 18);
        path.lineTo(11, 28);
        path.lineTo(15, 26);
        path.lineTo(11, 17);
        path.lineTo(19, 17);
        path.closeSubpath();
        p.setPen(QPen(Qt::black, 1.5));
        p.setBrush(Qt::white);
        p.drawPath(path);
        return image;
    }();
    return arrow;
}

#if defined(Q_OS_WIN)
// The cursor drawn into a bitmap with GDI, as Windows draws it.
bool winCursor(CursorCapture::Cursor &cursor)
{
    CURSORINFO info{};
    info.cbSize = sizeof(info);
    if (!GetCursorInfo(&info) || !(info.flags & CURSOR_SHOWING) || !info.hCursor)
        return false;
    ICONINFO icon{};
    if (!GetIconInfo(info.hCursor, &icon))
        return false;
    BITMAP bitmap{};
    GetObject(icon.hbmColor ? icon.hbmColor : icon.hbmMask, sizeof(bitmap), &bitmap);
    const int width = bitmap.bmWidth;
    // A monochrome cursor's mask bitmap holds the AND and XOR halves.
    const int height = icon.hbmColor ? bitmap.bmHeight : bitmap.bmHeight / 2;
    bool ok = false;
    if (width > 0 && height > 0) {
        HDC dc = CreateCompatibleDC(nullptr);
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = width;
        bi.bmiHeader.biHeight = -height;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        void *bits = nullptr;
        HBITMAP dib = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (dib && bits) {
            HGDIOBJ old = SelectObject(dc, dib);
            memset(bits, 0, size_t(width) * height * 4);
            DrawIconEx(dc, 0, 0, info.hCursor, width, height, 0, nullptr, DI_NORMAL);
            QImage image(static_cast<const uchar *>(bits), width, height, width * 4,
                         QImage::Format_ARGB32_Premultiplied);
            image = image.copy();
            // Without an alpha channel, what was drawn is what is opaque.
            bool hasAlpha = false;
            for (int y = 0; y < height && !hasAlpha; ++y) {
                const QRgb *row = reinterpret_cast<const QRgb *>(image.constScanLine(y));
                for (int x = 0; x < width && !hasAlpha; ++x)
                    hasAlpha = qAlpha(row[x]) != 0;
            }
            if (!hasAlpha) {
                for (int y = 0; y < height; ++y) {
                    QRgb *row = reinterpret_cast<QRgb *>(image.scanLine(y));
                    for (int x = 0; x < width; ++x)
                        row[x] = (row[x] & 0xffffff) ? (row[x] | 0xff000000) : 0;
                }
            }
            cursor.image = image;
            cursor.hotspot = QPoint(int(icon.xHotspot), int(icon.yHotspot));
            cursor.position = QCursor::pos();
            if (QScreen *screen = QGuiApplication::screenAt(cursor.position))
                cursor.imageDevicePixelRatio = screen->devicePixelRatio();
            cursor.ok = true;
            ok = true;
            SelectObject(dc, old);
        }
        if (dib)
            DeleteObject(dib);
        DeleteDC(dc);
    }
    if (icon.hbmColor)
        DeleteObject(icon.hbmColor);
    if (icon.hbmMask)
        DeleteObject(icon.hbmMask);
    return ok;
}
#endif

#ifdef GUCKSTE_HAVE_X11
Display *display()
{
    static Display *d = [] {
        if (!QGuiApplication::platformName().startsWith(QStringLiteral("xcb")))
            return static_cast<Display *>(nullptr);
        Display *opened = XOpenDisplay(nullptr);
        int eventBase = 0, errorBase = 0;
        if (opened && !XFixesQueryExtension(opened, &eventBase, &errorBase)) {
            XCloseDisplay(opened);
            opened = nullptr;
        }
        return opened;
    }();
    return d;
}
#endif

} // namespace

CursorCapture::Cursor CursorCapture::current()
{
    Cursor cursor;
#if defined(Q_OS_WIN)
    if (winCursor(cursor))
        return cursor;
#elif defined(Q_OS_MACOS)
    if (macCursor(cursor))
        return cursor;
#elif defined(GUCKSTE_HAVE_X11)
    if (Display *d = display()) {
        if (XFixesCursorImage *ci = XFixesGetCursorImage(d)) {
            QImage image(ci->width, ci->height, QImage::Format_ARGB32_Premultiplied);
            // One unsigned long per pixel, whatever its size.
            for (int y = 0; y < ci->height; ++y) {
                auto *row = reinterpret_cast<QRgb *>(image.scanLine(y));
                for (int x = 0; x < ci->width; ++x)
                    row[x] = QRgb(ci->pixels[y * ci->width + x] & 0xffffffffUL);
            }
            cursor.image = image;
            cursor.hotspot = QPoint(ci->xhot, ci->yhot);
            cursor.position = QPoint(ci->x, ci->y);
            cursor.ok = true;
            XFree(ci);
            return cursor;
        }
    }
#endif
    cursor.image = fallbackArrow();
    cursor.hotspot = QPoint(1, 1);
    cursor.position = QCursor::pos();
    cursor.ok = true;
    return cursor;
}

void CursorCapture::paint(QImage &target, const QRect &area, qreal devicePixelRatio, const Cursor &cursor)
{
    if (!cursor.ok || cursor.image.isNull() || target.isNull())
        return;
    const qreal imageRatio = cursor.imageDevicePixelRatio > 0 ? cursor.imageDevicePixelRatio : 1;
    const QPointF at = (QPointF(cursor.position - area.topLeft()) - QPointF(cursor.hotspot) / imageRatio)
                       * devicePixelRatio;
    const QRectF drawn(at, QSizeF(cursor.image.size()) / imageRatio * devicePixelRatio);
    if (!drawn.intersects(QRectF(target.rect())))
        return;
    QPainter p(&target);
    p.setRenderHint(QPainter::SmoothPixmapTransform, !qFuzzyCompare(devicePixelRatio, imageRatio));
    p.drawImage(drawn, cursor.image);
}
