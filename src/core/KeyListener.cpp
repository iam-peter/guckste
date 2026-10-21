#include "KeyListener.h"

#include <QGuiApplication>
#include <QHash>

#if defined(Q_OS_WIN)
#include <windows.h>
#elif defined(Q_OS_MACOS)
bool macKeyTapAllowed();
void runMacKeyTap(KeyListener *listener, std::atomic<bool> &stop);
#elif defined(GUCKSTE_HAVE_X11)
#include <X11/XKBlib.h>
#include <X11/Xlib.h>
#include <X11/extensions/XInput2.h>
#include <X11/keysym.h>
#include <poll.h>
#endif

KeyListener::KeyListener(QObject *parent)
    : QThread(parent)
{
}

KeyListener::~KeyListener()
{
    end();
}

bool KeyListener::supported()
{
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    return true;
#elif defined(GUCKSTE_HAVE_X11)
    return QGuiApplication::platformName().startsWith(QStringLiteral("xcb"));
#else
    return false;
#endif
}

QString KeyListener::requirement()
{
#if defined(Q_OS_MACOS)
    return tr("Allow Guckste under Accessibility in System Settings → Privacy & Security to show keys.");
#else
    return {};
#endif
}

bool KeyListener::begin()
{
    if (!supported() || isRunning())
        return false;
#if defined(Q_OS_MACOS)
    if (!macKeyTapAllowed())
        return false;
#endif
    m_stop = false;
    start();
    return true;
}

void KeyListener::end()
{
    m_stop = true;
    if (isRunning())
        wait(2000);
}

QString KeyListener::label(const QString &keyName, bool shift, bool ctrl, bool alt, bool super)
{
    static const QHash<QString, QString> names{
        {QStringLiteral("Return"), QStringLiteral("Enter")},   {QStringLiteral("KP_Enter"), QStringLiteral("Enter")},
        {QStringLiteral("space"), QStringLiteral("Space")},    {QStringLiteral("BackSpace"), QStringLiteral("⌫")},
        {QStringLiteral("Delete"), QStringLiteral("Del")},     {QStringLiteral("Escape"), QStringLiteral("Esc")},
        {QStringLiteral("Tab"), QStringLiteral("Tab")},        {QStringLiteral("ISO_Left_Tab"), QStringLiteral("Tab")},
        {QStringLiteral("Left"), QStringLiteral("←")},         {QStringLiteral("Right"), QStringLiteral("→")},
        {QStringLiteral("Up"), QStringLiteral("↑")},           {QStringLiteral("Down"), QStringLiteral("↓")},
        {QStringLiteral("Home"), QStringLiteral("Home")},      {QStringLiteral("End"), QStringLiteral("End")},
        {QStringLiteral("Prior"), QStringLiteral("PgUp")},     {QStringLiteral("Next"), QStringLiteral("PgDn")},
        {QStringLiteral("Insert"), QStringLiteral("Ins")},     {QStringLiteral("Print"), QStringLiteral("Print")},
        {QStringLiteral("Caps_Lock"), QStringLiteral("Caps")}, {QStringLiteral("Menu"), QStringLiteral("Menu")},
        {QStringLiteral("minus"), QStringLiteral("-")},        {QStringLiteral("plus"), QStringLiteral("+")},
        {QStringLiteral("equal"), QStringLiteral("=")},        {QStringLiteral("comma"), QStringLiteral(",")},
        {QStringLiteral("period"), QStringLiteral(".")},       {QStringLiteral("slash"), QStringLiteral("/")},
        {QStringLiteral("backslash"), QStringLiteral("\\")},   {QStringLiteral("semicolon"), QStringLiteral(";")},
        {QStringLiteral("colon"), QStringLiteral(":")},        {QStringLiteral("apostrophe"), QStringLiteral("'")},
        {QStringLiteral("quotedbl"), QStringLiteral("\"")},    {QStringLiteral("grave"), QStringLiteral("`")},
        {QStringLiteral("asciitilde"), QStringLiteral("~")},   {QStringLiteral("bracketleft"), QStringLiteral("[")},
        {QStringLiteral("bracketright"), QStringLiteral("]")}, {QStringLiteral("braceleft"), QStringLiteral("{")},
        {QStringLiteral("braceright"), QStringLiteral("}")},   {QStringLiteral("parenleft"), QStringLiteral("(")},
        {QStringLiteral("parenright"), QStringLiteral(")")},   {QStringLiteral("less"), QStringLiteral("<")},
        {QStringLiteral("greater"), QStringLiteral(">")},      {QStringLiteral("question"), QStringLiteral("?")},
        {QStringLiteral("exclam"), QStringLiteral("!")},       {QStringLiteral("at"), QStringLiteral("@")},
        {QStringLiteral("numbersign"), QStringLiteral("#")},   {QStringLiteral("dollar"), QStringLiteral("$")},
        {QStringLiteral("percent"), QStringLiteral("%")},      {QStringLiteral("asciicircum"), QStringLiteral("^")},
        {QStringLiteral("ampersand"), QStringLiteral("&")},    {QStringLiteral("asterisk"), QStringLiteral("*")},
        {QStringLiteral("underscore"), QStringLiteral("_")},   {QStringLiteral("bar"), QStringLiteral("|")},
        {QStringLiteral("ssharp"), QStringLiteral("ß")},       {QStringLiteral("adiaeresis"), QStringLiteral("ä")},
        {QStringLiteral("odiaeresis"), QStringLiteral("ö")},   {QStringLiteral("udiaeresis"), QStringLiteral("ü")},
        {QStringLiteral("Adiaeresis"), QStringLiteral("Ä")},   {QStringLiteral("Odiaeresis"), QStringLiteral("Ö")},
        {QStringLiteral("Udiaeresis"), QStringLiteral("Ü")},
    };
    QString key = names.value(keyName, keyName);
    if (key.startsWith(QStringLiteral("KP_")))
        key = key.mid(3);
    // A letter on its own is typed as it comes; with Ctrl or Alt it is a
    // shortcut and reads as its capital.
    const bool shortcut = ctrl || alt || super;
    if (key.size() == 1) {
        if (shortcut)
            key = key.toUpper();
    } else if (!shortcut && keyName.size() == 1) {
        key = keyName;
    }
    QStringList parts;
    if (ctrl)
        parts << QStringLiteral("Ctrl");
    if (alt)
        parts << QStringLiteral("Alt");
    if (super) {
#if defined(Q_OS_MACOS)
        parts << QStringLiteral("Cmd");
#elif defined(Q_OS_WIN)
        parts << QStringLiteral("Win");
#else
        parts << QStringLiteral("Super");
#endif
    }
    // Shift shows only where it is not already in the character.
    if (shift && (shortcut || key.size() > 1))
        parts << QStringLiteral("Shift");
    parts << key;
    return parts.join(u'+');
}

#if defined(Q_OS_WIN)
namespace {

KeyListener *s_listener = nullptr;

bool isModifier(DWORD vk)
{
    switch (vk) {
    case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT:
    case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL:
    case VK_MENU: case VK_LMENU: case VK_RMENU:
    case VK_LWIN: case VK_RWIN: case VK_CAPITAL:
        return true;
    default:
        return false;
    }
}

// Windows' names for keys, in X11's spelling, which label() knows.
QString winKeyName(const KBDLLHOOKSTRUCT &key)
{
    switch (key.vkCode) {
    case VK_RETURN: return key.flags & LLKHF_EXTENDED ? QStringLiteral("KP_Enter") : QStringLiteral("Return");
    case VK_TAB: return QStringLiteral("Tab");
    case VK_SPACE: return QStringLiteral("space");
    case VK_BACK: return QStringLiteral("BackSpace");
    case VK_DELETE: return QStringLiteral("Delete");
    case VK_ESCAPE: return QStringLiteral("Escape");
    case VK_LEFT: return QStringLiteral("Left");
    case VK_RIGHT: return QStringLiteral("Right");
    case VK_UP: return QStringLiteral("Up");
    case VK_DOWN: return QStringLiteral("Down");
    case VK_HOME: return QStringLiteral("Home");
    case VK_END: return QStringLiteral("End");
    case VK_PRIOR: return QStringLiteral("Prior");
    case VK_NEXT: return QStringLiteral("Next");
    case VK_INSERT: return QStringLiteral("Insert");
    case VK_SNAPSHOT: return QStringLiteral("Print");
    default: break;
    }
    if (key.vkCode >= VK_F1 && key.vkCode <= VK_F24)
        return QStringLiteral("F%1").arg(key.vkCode - VK_F1 + 1);
    // What the key types, with the keyboard's state as it is.
    BYTE state[256] = {};
    GetKeyboardState(state);
    wchar_t chars[8] = {};
    const int length = ToUnicode(key.vkCode, key.scanCode, state, chars, 8, 0);
    if (length > 0)
        return QString::fromWCharArray(chars, length);
    wchar_t name[64] = {};
    const LONG lparam = LONG(key.scanCode << 16) | (key.flags & LLKHF_EXTENDED ? 1 << 24 : 0);
    if (GetKeyNameTextW(lparam, name, 64) > 0)
        return QString::fromWCharArray(name);
    return {};
}

LRESULT CALLBACK keyboardHook(int code, WPARAM wparam, LPARAM lparam)
{
    if (code == HC_ACTION && (wparam == WM_KEYDOWN || wparam == WM_SYSKEYDOWN) && s_listener) {
        const auto *key = reinterpret_cast<const KBDLLHOOKSTRUCT *>(lparam);
        if (!isModifier(key->vkCode)) {
            const bool shift = GetAsyncKeyState(VK_SHIFT) & 0x8000;
            const bool ctrl = GetAsyncKeyState(VK_CONTROL) & 0x8000;
            const bool alt = GetAsyncKeyState(VK_MENU) & 0x8000;
            const bool win = (GetAsyncKeyState(VK_LWIN) & 0x8000) || (GetAsyncKeyState(VK_RWIN) & 0x8000);
            QString name = winKeyName(*key);
            // A control character from ToUnicode under Ctrl is the letter.
            if (name.size() == 1 && name[0].unicode() < 32)
                name = QChar(ushort('a' + name[0].unicode() - 1));
            if (!name.isEmpty())
                emit s_listener->keyPressed(KeyListener::label(name, shift, ctrl, alt, win));
        }
    }
    return CallNextHookEx(nullptr, code, wparam, lparam);
}

} // namespace
#endif

void KeyListener::run()
{
#if defined(Q_OS_WIN)
    s_listener = this;
    HHOOK hook = SetWindowsHookExW(WH_KEYBOARD_LL, keyboardHook, GetModuleHandleW(nullptr), 0);
    if (!hook) {
        s_listener = nullptr;
        return;
    }
    // The hook needs this thread to pump messages.
    while (!m_stop) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Sleep(20);
    }
    UnhookWindowsHookEx(hook);
    s_listener = nullptr;
#elif defined(Q_OS_MACOS)
    runMacKeyTap(this, m_stop);
#elif defined(GUCKSTE_HAVE_X11)
    Display *d = XOpenDisplay(nullptr);
    if (!d)
        return;
    int opcode = 0, event = 0, error = 0;
    if (!XQueryExtension(d, "XInputExtension", &opcode, &event, &error)) {
        XCloseDisplay(d);
        return;
    }
    int major = 2, minor = 0;
    if (XIQueryVersion(d, &major, &minor) != Success) {
        XCloseDisplay(d);
        return;
    }
    unsigned char bits[XIMaskLen(XI_LASTEVENT)] = {};
    XIEventMask mask;
    mask.deviceid = XIAllMasterDevices;
    mask.mask_len = sizeof(bits);
    mask.mask = bits;
    XISetMask(bits, XI_RawKeyPress);
    const Window root = DefaultRootWindow(d);
    XISelectEvents(d, root, &mask, 1);
    XFlush(d);

    pollfd fd{ConnectionNumber(d), POLLIN, 0};
    while (!m_stop) {
        if (!XPending(d)) {
            poll(&fd, 1, 100);
            continue;
        }
        XEvent ev;
        XNextEvent(d, &ev);
        if (ev.xcookie.type != GenericEvent || ev.xcookie.extension != opcode || !XGetEventData(d, &ev.xcookie))
            continue;
        if (ev.xcookie.evtype == XI_RawKeyPress) {
            auto *raw = static_cast<XIRawEvent *>(ev.xcookie.data);
            const int keycode = raw->detail;
            // The modifiers at this moment, from the pointer query.
            Window w1, w2;
            int i1, i2, i3, i4;
            unsigned int state = 0;
            XQueryPointer(d, root, &w1, &w2, &i1, &i2, &i3, &i4, &state);
            const bool shift = state & ShiftMask;
            const bool ctrl = state & ControlMask;
            const bool alt = state & Mod1Mask;
            const bool super = state & Mod4Mask;
            const KeySym keysym = XkbKeycodeToKeysym(d, keycode, 0, shift ? 1 : 0);
            const bool modifierKey = (keysym >= XK_Shift_L && keysym <= XK_Hyper_R) || keysym == XK_Caps_Lock
                                     || keysym == XK_ISO_Level3_Shift || keysym == XK_Mode_switch;
            if (keysym != NoSymbol && !modifierKey) {
                const char *name = XKeysymToString(keysym);
                if (name)
                    emit keyPressed(label(QString::fromLatin1(name), shift, ctrl, alt, super));
            }
        }
        XFreeEventData(d, &ev.xcookie);
    }
    XCloseDisplay(d);
#endif
}
