#include "KeyListener.h"

#import <ApplicationServices/ApplicationServices.h>
#import <Carbon/Carbon.h>

// Keys from an event tap, which macOS allows once the app has the
// Accessibility permission (System Settings → Privacy & Security).
bool macKeyTapAllowed()
{
    return AXIsProcessTrusted();
}

namespace {

QString keyName(CGKeyCode code, bool shift)
{
    switch (code) {
    case kVK_Return: return QStringLiteral("Return");
    case kVK_ANSI_KeypadEnter: return QStringLiteral("KP_Enter");
    case kVK_Tab: return QStringLiteral("Tab");
    case kVK_Space: return QStringLiteral("space");
    case kVK_Delete: return QStringLiteral("BackSpace");
    case kVK_ForwardDelete: return QStringLiteral("Delete");
    case kVK_Escape: return QStringLiteral("Escape");
    case kVK_LeftArrow: return QStringLiteral("Left");
    case kVK_RightArrow: return QStringLiteral("Right");
    case kVK_UpArrow: return QStringLiteral("Up");
    case kVK_DownArrow: return QStringLiteral("Down");
    case kVK_Home: return QStringLiteral("Home");
    case kVK_End: return QStringLiteral("End");
    case kVK_PageUp: return QStringLiteral("Prior");
    case kVK_PageDown: return QStringLiteral("Next");
    case kVK_F1: return QStringLiteral("F1");
    case kVK_F2: return QStringLiteral("F2");
    case kVK_F3: return QStringLiteral("F3");
    case kVK_F4: return QStringLiteral("F4");
    case kVK_F5: return QStringLiteral("F5");
    case kVK_F6: return QStringLiteral("F6");
    case kVK_F7: return QStringLiteral("F7");
    case kVK_F8: return QStringLiteral("F8");
    case kVK_F9: return QStringLiteral("F9");
    case kVK_F10: return QStringLiteral("F10");
    case kVK_F11: return QStringLiteral("F11");
    case kVK_F12: return QStringLiteral("F12");
    default: break;
    }
    // What the key types in the current layout.
    TISInputSourceRef source = TISCopyCurrentKeyboardLayoutInputSource();
    if (!source)
        return {};
    auto data = static_cast<CFDataRef>(TISGetInputSourceProperty(source, kTISPropertyUnicodeKeyLayoutData));
    QString name;
    if (data) {
        auto layout = reinterpret_cast<const UCKeyboardLayout *>(CFDataGetBytePtr(data));
        UInt32 deadKeys = 0;
        UniChar chars[4] = {};
        UniCharCount length = 0;
        const UInt32 modifiers = shift ? (shiftKey >> 8) & 0xff : 0;
        if (UCKeyTranslate(layout, code, kUCKeyActionDisplay, modifiers, LMGetKbdType(), kUCKeyTranslateNoDeadKeysBit,
                           &deadKeys, 4, &length, chars) == noErr && length > 0)
            name = QString::fromUtf16(reinterpret_cast<const char16_t *>(chars), int(length));
    }
    CFRelease(source);
    return name;
}

CGEventRef tapCallback(CGEventTapProxy, CGEventType type, CGEventRef event, void *refcon)
{
    auto *listener = static_cast<KeyListener *>(refcon);
    if (type == kCGEventLeftMouseDown || type == kCGEventRightMouseDown || type == kCGEventOtherMouseDown) {
        emit listener->buttonPressed();
    } else if (type == kCGEventKeyDown) {
        const auto code = CGKeyCode(CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode));
        const CGEventFlags flags = CGEventGetFlags(event);
        const bool shift = flags & kCGEventFlagMaskShift;
        const bool ctrl = flags & kCGEventFlagMaskControl;
        const bool alt = flags & kCGEventFlagMaskAlternate;
        const bool command = flags & kCGEventFlagMaskCommand;
        const QString name = keyName(code, shift);
        if (!name.isEmpty())
            emit listener->keyPressed(KeyListener::label(name, shift, ctrl, alt, command));
    }
    return event;
}

} // namespace

void runMacKeyTap(KeyListener *listener, std::atomic<bool> &stop, bool buttons)
{
    CGEventMask mask = CGEventMaskBit(kCGEventKeyDown);
    if (buttons)
        mask |= CGEventMaskBit(kCGEventLeftMouseDown) | CGEventMaskBit(kCGEventRightMouseDown)
                | CGEventMaskBit(kCGEventOtherMouseDown);
    CFMachPortRef tap = CGEventTapCreate(kCGSessionEventTap, kCGHeadInsertEventTap, kCGEventTapOptionListenOnly,
                                         mask, tapCallback, listener);
    if (!tap)
        return;
    CFRunLoopSourceRef source = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, tap, 0);
    CFRunLoopAddSource(CFRunLoopGetCurrent(), source, kCFRunLoopCommonModes);
    CGEventTapEnable(tap, true);
    while (!stop)
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.1, false);
    CGEventTapEnable(tap, false);
    CFRunLoopRemoveSource(CFRunLoopGetCurrent(), source, kCFRunLoopCommonModes);
    CFRelease(source);
    CFRelease(tap);
}
