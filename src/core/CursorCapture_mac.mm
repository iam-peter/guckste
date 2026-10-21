#include "CursorCapture.h"

#include <QCursor>

#import <AppKit/AppKit.h>

// The cursor as macOS shows it, from AppKit.
bool macCursor(CursorCapture::Cursor &cursor)
{
    @autoreleasepool {
        NSCursor *current = [NSCursor currentSystemCursor];
        if (!current)
            return false;
        NSImage *image = current.image;
        if (!image)
            return false;
        CGImageRef cg = [image CGImageForProposedRect:nullptr context:nil hints:nil];
        if (!cg)
            return false;
        cursor.image = QImage::fromCGImage(cg).convertToFormat(QImage::Format_ARGB32_Premultiplied);
        if (cursor.image.isNull())
            return false;
        const NSPoint hot = current.hotSpot;
        cursor.hotspot = QPoint(int(hot.x), int(hot.y));
        cursor.position = QCursor::pos();
        // The image may be a Retina one, bigger than its size in points.
        cursor.imageDevicePixelRatio = image.size.width > 0 ? cursor.image.width() / image.size.width : 1;
        cursor.ok = true;
        return true;
    }
}
