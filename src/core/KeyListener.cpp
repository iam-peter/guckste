#include "KeyListener.h"

#include <QGuiApplication>
#include <QHash>

#ifdef GUCKSTE_HAVE_X11
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
#ifdef GUCKSTE_HAVE_X11
    return QGuiApplication::platformName().startsWith(QStringLiteral("xcb"));
#else
    return false;
#endif
}

bool KeyListener::begin()
{
    if (!supported() || isRunning())
        return false;
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
    if (super)
        parts << QStringLiteral("Super");
    // Shift shows only where it is not already in the character.
    if (shift && (shortcut || key.size() > 1))
        parts << QStringLiteral("Shift");
    parts << key;
    return parts.join(u'+');
}

void KeyListener::run()
{
#ifdef GUCKSTE_HAVE_X11
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
