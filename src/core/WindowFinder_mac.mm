#include <QList>
#include <QRect>

#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>

// The on-screen windows of other applications, front to back, as
// CGWindowList reports them: in points, which Qt's logical pixels are.
QList<QRect> macWindows()
{
    QList<QRect> result;
    CFArrayRef list = CGWindowListCopyWindowInfo(kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements,
                                                 kCGNullWindowID);
    if (!list)
        return result;
    const pid_t self = getpid();
    const CFIndex count = CFArrayGetCount(list);
    for (CFIndex i = 0; i < count; ++i) {
        auto info = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(list, i));
        auto layer = static_cast<CFNumberRef>(CFDictionaryGetValue(info, kCGWindowLayer));
        int layerValue = 0;
        if (layer)
            CFNumberGetValue(layer, kCFNumberIntType, &layerValue);
        if (layerValue != 0)
            continue;
        auto pid = static_cast<CFNumberRef>(CFDictionaryGetValue(info, kCGWindowOwnerPID));
        int pidValue = 0;
        if (pid)
            CFNumberGetValue(pid, kCFNumberIntType, &pidValue);
        if (pidValue == self)
            continue;
        auto bounds = static_cast<CFDictionaryRef>(CFDictionaryGetValue(info, kCGWindowBounds));
        CGRect rect;
        if (!bounds || !CGRectMakeWithDictionaryRepresentation(bounds, &rect))
            continue;
        if (rect.size.width > 1 && rect.size.height > 1)
            result.append(QRect(int(rect.origin.x), int(rect.origin.y), int(rect.size.width), int(rect.size.height)));
    }
    CFRelease(list);
    return result;
}
