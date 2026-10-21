#include "WindowFinder.h"

#include <QGuiApplication>
#include <QScreen>

#if defined(Q_OS_WIN)
#include <dwmapi.h>
#include <windows.h>
#elif defined(Q_OS_MACOS)
QList<QRect> macWindows();
#elif defined(GUCKSTE_HAVE_X11)
#include <X11/Xlib.h>
#endif

namespace {

// A window the size of the desktop or a screen is a desktop, not a pick.
bool isDesktopSized(const QRect &r)
{
    QRect all;
    for (QScreen *screen : QGuiApplication::screens()) {
        if (r == screen->geometry())
            return true;
        all |= screen->geometry();
    }
    return r == all;
}

} // namespace

QList<QRect> WindowFinder::windows()
{
    QList<QRect> result;
#if defined(Q_OS_WIN)
    struct Context
    {
        QList<QRect> *out;
    } context{&result};
    EnumWindows(
        [](HWND hwnd, LPARAM lparam) -> BOOL {
            auto *ctx = reinterpret_cast<Context *>(lparam);
            if (!IsWindowVisible(hwnd) || IsIconic(hwnd))
                return TRUE;
            if (GetWindowTextLengthW(hwnd) == 0)
                return TRUE;
            // Cloaked windows are not on screen, whatever IsWindowVisible says.
            DWORD cloaked = 0;
            DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
            if (cloaked)
                return TRUE;
            RECT rect{};
            if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &rect, sizeof(rect))))
                GetWindowRect(hwnd, &rect);
            // Physical pixels; the screen's ratio makes them logical.
            qreal ratio = 1;
            if (QScreen *screen = QGuiApplication::screenAt(QPoint(rect.left, rect.top)))
                ratio = screen->devicePixelRatio();
            const QRect r(qRound(rect.left / ratio), qRound(rect.top / ratio), qRound((rect.right - rect.left) / ratio),
                          qRound((rect.bottom - rect.top) / ratio));
            if (r.width() > 1 && r.height() > 1)
                ctx->out->append(r);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&context));
#elif defined(Q_OS_MACOS)
    result = macWindows();
#elif defined(GUCKSTE_HAVE_X11)
    if (!QGuiApplication::platformName().startsWith(QStringLiteral("xcb")))
        return result;
    Display *d = XOpenDisplay(nullptr);
    if (!d)
        return result;
    const Window root = DefaultRootWindow(d);
    Window parent = 0, rootReturn = 0;
    Window *children = nullptr;
    unsigned int count = 0;
    if (XQueryTree(d, root, &rootReturn, &parent, &children, &count) && children) {
        // Bottom to top as X lists them; front to back for us.
        for (int i = int(count) - 1; i >= 0; --i) {
            XWindowAttributes attributes;
            if (!XGetWindowAttributes(d, children[i], &attributes))
                continue;
            if (attributes.map_state != IsViewable || attributes.override_redirect)
                continue;
            const QRect r(attributes.x, attributes.y, attributes.width, attributes.height);
            if (r.width() > 1 && r.height() > 1)
                result.append(r);
        }
        XFree(children);
    }
    XCloseDisplay(d);
#endif
    result.erase(std::remove_if(result.begin(), result.end(), isDesktopSized), result.end());
    return result;
}
