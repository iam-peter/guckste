// The whole X root window in one transfer through shared memory. Qt grabs
// each screen with GetImage, copying every pixel through the X socket,
// which takes several times as long on a large desktop. Kept apart from
// ScreenCapture.cpp, where Xlib's macros would clash with Qt names.

#include <QGuiApplication>
#include <QImage>
#include <QSize>
#include <QSysInfo>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>
#include <sys/ipc.h>
#include <sys/shm.h>

QImage grabRootWithShm(const QSize &expected)
{
    auto *x11 = qGuiApp->nativeInterface<QNativeInterface::QX11Application>();
    Display *display = x11 ? x11->display() : nullptr;
    if (!display || !XShmQueryExtension(display))
        return {};
    const Window root = DefaultRootWindow(display);
    XWindowAttributes attributes;
    if (!XGetWindowAttributes(display, root, &attributes)
        || QSize(attributes.width, attributes.height) != expected || attributes.depth < 24)
        return {};

    XShmSegmentInfo segment {};
    XImage *ximage = XShmCreateImage(display, attributes.visual, attributes.depth, ZPixmap, nullptr, &segment,
                                     attributes.width, attributes.height);
    if (!ximage)
        return {};
    // Only the usual layout, 0x00RRGGBB in native byte order; anything
    // else is left to Qt.
    const int nativeOrder = QSysInfo::ByteOrder == QSysInfo::LittleEndian ? LSBFirst : MSBFirst;
    if (ximage->bits_per_pixel != 32 || ximage->byte_order != nativeOrder || ximage->red_mask != 0xff0000
        || ximage->green_mask != 0xff00 || ximage->blue_mask != 0xff) {
        XDestroyImage(ximage);
        return {};
    }

    QImage result;
    segment.shmid = shmget(IPC_PRIVATE, size_t(ximage->bytes_per_line) * ximage->height, IPC_CREAT | 0600);
    if (segment.shmid >= 0) {
        segment.shmaddr = ximage->data = static_cast<char *>(shmat(segment.shmid, nullptr, 0));
        segment.readOnly = False;
        if (segment.shmaddr != reinterpret_cast<char *>(-1) && XShmAttach(display, &segment)) {
            if (XShmGetImage(display, root, ximage, 0, 0, AllPlanes)) {
                // The top byte is undefined; make it opaque.
                result = QImage(ximage->width, ximage->height, QImage::Format_ARGB32_Premultiplied);
                for (int y = 0; y < ximage->height; ++y) {
                    const auto *from = reinterpret_cast<const quint32 *>(ximage->data + qsizetype(y) * ximage->bytes_per_line);
                    auto *to = reinterpret_cast<quint32 *>(result.scanLine(y));
                    for (int x = 0; x < ximage->width; ++x)
                        to[x] = from[x] | 0xff000000u;
                }
            }
            XShmDetach(display, &segment);
            XSync(display, False);
        }
        if (segment.shmaddr != reinterpret_cast<char *>(-1))
            shmdt(segment.shmaddr);
        // Removed now; it goes once detached.
        shmctl(segment.shmid, IPC_RMID, nullptr);
    }
    ximage->data = nullptr;
    XDestroyImage(ximage);
    return result;
}
