#include "CursorCapture.h"

#include <QCursor>
#include <QGuiApplication>
#include <QPainter>
#include <QPainterPath>

#ifdef GUCKSTE_HAVE_X11
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
#ifdef GUCKSTE_HAVE_X11
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
    const QPointF at = (cursor.position - area.topLeft() - cursor.hotspot) * devicePixelRatio;
    const QRectF drawn(at, cursor.image.size() * devicePixelRatio);
    if (!drawn.intersects(QRectF(target.rect())))
        return;
    QPainter p(&target);
    p.setRenderHint(QPainter::SmoothPixmapTransform, devicePixelRatio != 1);
    p.drawImage(drawn, cursor.image);
}
