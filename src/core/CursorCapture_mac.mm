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
        // Drawn into a QImage through a bitmap context over its bytes.
        const size_t width = CGImageGetWidth(cg), height = CGImageGetHeight(cg);
        if (width == 0 || height == 0)
            return false;
        QImage converted(int(width), int(height), QImage::Format_ARGB32_Premultiplied);
        converted.fill(Qt::transparent);
        CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
        CGContextRef context = CGBitmapContextCreate(converted.bits(), width, height, 8, size_t(converted.bytesPerLine()),
                                                     colorSpace, kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Host);
        CGColorSpaceRelease(colorSpace);
        if (!context)
            return false;
        CGContextDrawImage(context, CGRectMake(0, 0, CGFloat(width), CGFloat(height)), cg);
        CGContextRelease(context);
        cursor.image = converted;
        const NSPoint hot = current.hotSpot;
        cursor.hotspot = QPoint(int(hot.x), int(hot.y));
        cursor.position = QCursor::pos();
        // The image may be a Retina one, bigger than its size in points.
        cursor.imageDevicePixelRatio = image.size.width > 0 ? cursor.image.width() / image.size.width : 1;
        cursor.ok = true;
        return true;
    }
}
