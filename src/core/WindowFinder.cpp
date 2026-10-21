#include "WindowFinder.h"

#include <QGuiApplication>
#include <QLoggingCategory>
#include <QRegion>
#include <QScreen>

#if defined(Q_OS_WIN)
#include <dwmapi.h>
#include <windows.h>
#elif defined(Q_OS_MACOS)
QList<QRect> macWindows();
#elif defined(GUCKSTE_HAVE_X11)
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#endif

Q_LOGGING_CATEGORY(lcWindows, "guckste.windows")

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
            // The desktop and its workers are not windows to pick.
            wchar_t className[64] = {};
            GetClassNameW(hwnd, className, 64);
            const QString cls = QString::fromWCharArray(className);
            if (cls == QStringLiteral("Progman") || cls == QStringLiteral("WorkerW"))
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
    // The window manager's list of managed windows, bottom to top, rather
    // than the window tree, which has helper windows nobody sees.
    const Atom clientList = XInternAtom(d, "_NET_CLIENT_LIST_STACKING", False);
    const Atom frameExtents = XInternAtom(d, "_NET_FRAME_EXTENTS", False);
    const Atom wmState = XInternAtom(d, "_NET_WM_STATE", False);
    const Atom hidden = XInternAtom(d, "_NET_WM_STATE_HIDDEN", False);
    const Atom wmDesktop = XInternAtom(d, "_NET_WM_DESKTOP", False);
    const Atom currentDesktop = XInternAtom(d, "_NET_CURRENT_DESKTOP", False);
    const Atom windowType = XInternAtom(d, "_NET_WM_WINDOW_TYPE", False);
    const Atom desktopType = XInternAtom(d, "_NET_WM_WINDOW_TYPE_DESKTOP", False);

    auto property = [d](Window w, Atom name, Atom type) {
        QList<unsigned long> values;
        Atom actualType = None;
        int format = 0;
        unsigned long count = 0, after = 0;
        unsigned char *data = nullptr;
        if (XGetWindowProperty(d, w, name, 0, 1024, False, type, &actualType, &format, &count, &after, &data) == Success
            && data) {
            if (format == 32) {
                // Xlib widens 32-bit values to long, sign and all.
                const auto *items = reinterpret_cast<const unsigned long *>(data);
                for (unsigned long i = 0; i < count; ++i)
                    values << (items[i] & 0xFFFFFFFFUL);
            }
            XFree(data);
        }
        return values;
    };

    const QList<unsigned long> current = property(root, currentDesktop, XA_CARDINAL);
    const unsigned long desktop = current.isEmpty() ? 0 : current.first();
    const QList<unsigned long> clients = property(root, clientList, XA_WINDOW);
    for (int i = clients.size() - 1; i >= 0; --i) {
        const Window w = Window(clients[i]);
        XWindowAttributes attributes;
        if (!XGetWindowAttributes(d, w, &attributes) || attributes.map_state != IsViewable) {
            qCDebug(lcWindows) << Qt::hex << w << "not viewable";
            continue;
        }
        if (property(w, wmState, XA_ATOM).contains(hidden)) {
            qCDebug(lcWindows) << Qt::hex << w << "hidden";
            continue;
        }
        if (property(w, windowType, XA_ATOM).contains(desktopType)) {
            qCDebug(lcWindows) << Qt::hex << w << "desktop";
            continue;
        }
        const QList<unsigned long> onDesktop = property(w, wmDesktop, XA_CARDINAL);
        if (!onDesktop.isEmpty() && onDesktop.first() != desktop && onDesktop.first() != 0xFFFFFFFFUL) {
            qCDebug(lcWindows) << Qt::hex << w << "on another desktop";
            continue;
        }
        // The client's place on the root, and the frame around it.
        int x = 0, y = 0;
        Window child = None;
        XTranslateCoordinates(d, w, root, 0, 0, &x, &y, &child);
        QRect r(x, y, attributes.width, attributes.height);
        const QList<unsigned long> extents = property(w, frameExtents, XA_CARDINAL);
        if (extents.size() == 4)
            r.adjust(-int(extents[0]), -int(extents[2]), int(extents[1]), int(extents[3]));
        qCDebug(lcWindows) << Qt::hex << w << "at" << r;
        if (r.width() > 1 && r.height() > 1)
            result.append(r);
    }
    XCloseDisplay(d);
#endif
    // A window entirely behind the ones in front of it cannot be picked.
    QList<QRect> visible;
    QRegion covered;
    for (const QRect &r : std::as_const(result)) {
        if ((QRegion(r) - covered).isEmpty()) {
            qCDebug(lcWindows) << "covered:" << r;
            continue;
        }
        visible.append(r);
        covered += r;
    }
    return visible;
}
