#pragma once

#include <QImage>
#include <QPoint>
#include <QRect>

// The pointer as it is on screen, to paint into screenshots and recorded
// frames, which Qt leaves it out of. X11's XFixes extension, Windows'
// cursor info and macOS's AppKit have the real cursor image; on Wayland a
// plain arrow stands in for it.
namespace CursorCapture {

struct Cursor
{
    QImage image;     // premultiplied ARGB
    QPoint hotspot;   // in the image
    QPoint position;  // global, logical pixels
    // Image pixels per logical pixel: a Retina or HiDPI cursor is bigger
    // than the size it is shown at.
    qreal imageDevicePixelRatio = 1;
    bool ok = false;
};

Cursor current();

// Paints the pointer into `target`, which shows `area` of the desktop
// (logical pixels) at `devicePixelRatio`.
void paint(QImage &target, const QRect &area, qreal devicePixelRatio, const Cursor &cursor = current());

} // namespace CursorCapture
