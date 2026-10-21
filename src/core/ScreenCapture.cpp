#include "ScreenCapture.h"

#include "CursorCapture.h"

#include <QGuiApplication>
#include <QPainter>
#include <QPixmap>
#include <QScreen>
#include <QTimer>

#ifdef GUCKSTE_HAVE_DBUS
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QFile>
#include <QRandomGenerator>
#include <QUrl>
#endif

#ifdef GUCKSTE_HAVE_XSHM
QImage grabRootWithShm(const QSize &expected);
#endif

Capture ScreenCapture::grabScreens(bool includeCursor)
{
    Capture capture;
    const QList<QScreen *> screens = QGuiApplication::screens();
    if (screens.isEmpty())
        return capture;
    QRect geometry;
    for (QScreen *screen : screens)
        geometry |= screen->geometry();
    // The image is in device pixels, so a HiDPI desktop is captured at
    // full resolution; the overlay maps its logical size onto it.
    const qreal ratio = screens.first()->devicePixelRatio();
    QImage image;
#ifdef GUCKSTE_HAVE_XSHM
    // One X screen holding every monitor, as usual: all of it at once.
    if (QGuiApplication::platformName() == QStringLiteral("xcb") && ratio == 1 && geometry.topLeft().isNull()) {
        image = grabRootWithShm(geometry.size());
        capture.ok = !image.isNull();
    }
#endif
    if (image.isNull()) {
        image = QImage(geometry.size() * ratio, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::black);
        QPainter painter(&image);
        for (QScreen *screen : screens) {
            const QPixmap shot = screen->grabWindow(0);
            if (shot.isNull())
                continue;
            const QPoint at = (screen->geometry().topLeft() - geometry.topLeft()) * ratio;
            painter.drawPixmap(QRect(at, screen->geometry().size() * ratio), shot);
            capture.ok = true;
        }
    }
    if (includeCursor)
        CursorCapture::paint(image, geometry, ratio);
    capture.image = image;
    capture.geometry = geometry;
    return capture;
}

#ifdef GUCKSTE_HAVE_DBUS
namespace {

// One call of org.freedesktop.portal.Screenshot: the portal answers with
// the Response signal of a request object, bringing the URI of a PNG of
// all screens.
class PortalRequest : public QObject
{
    Q_OBJECT

public:
    PortalRequest(QObject *parent, std::function<void(const Capture &)> done)
        : QObject(parent), m_done(std::move(done))
    {
    }

    void start()
    {
        QDBusConnection bus = QDBusConnection::sessionBus();
        const QString token = QStringLiteral("guckste%1").arg(QRandomGenerator::global()->generate());
        QString sender = bus.baseService().mid(1);
        sender.replace(u'.', u'_');
        const QString requestPath
            = QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2").arg(sender, token);
        bus.connect(QStringLiteral("org.freedesktop.portal.Desktop"), requestPath,
                    QStringLiteral("org.freedesktop.portal.Request"), QStringLiteral("Response"), this,
                    SLOT(response(uint, QVariantMap)));

        QDBusInterface portal(QStringLiteral("org.freedesktop.portal.Desktop"),
                              QStringLiteral("/org/freedesktop/portal/desktop"),
                              QStringLiteral("org.freedesktop.portal.Screenshot"), bus);
        const QVariantMap options{{QStringLiteral("handle_token"), token},
                                  {QStringLiteral("interactive"), false}};
        auto *watcher = new QDBusPendingCallWatcher(
            portal.asyncCall(QStringLiteral("Screenshot"), QString(), options), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
            QDBusPendingReply<QDBusObjectPath> reply = *w;
            w->deleteLater();
            if (reply.isError()) {
                qWarning("Screenshot portal: %s", qPrintable(reply.error().message()));
                finish(Capture());
            }
        });
    }

public slots:
    void response(uint code, const QVariantMap &results)
    {
        Capture capture;
        if (code == 0) {
            const QString path = results.value(QStringLiteral("uri")).toUrl().toLocalFile();
            capture.image = QImage(path);
            QFile::remove(path);
            capture.ok = !capture.image.isNull();
            for (QScreen *screen : QGuiApplication::screens())
                capture.geometry |= screen->geometry();
        } else {
            qWarning("Screenshot portal answered %u", code);
        }
        finish(capture);
    }

private:
    void finish(const Capture &capture)
    {
        if (!m_done)
            return;
        auto done = std::move(m_done);
        m_done = nullptr;
        deleteLater();
        done(capture);
    }

    std::function<void(const Capture &)> m_done;
};

} // namespace
#endif

void ScreenCapture::grab(QObject *context, bool includeCursor, std::function<void(const Capture &)> done)
{
#ifdef GUCKSTE_HAVE_DBUS
    if (QGuiApplication::platformName().startsWith(QStringLiteral("wayland"))) {
        (new PortalRequest(context, std::move(done)))->start();
        return;
    }
#endif
    const Capture capture = grabScreens(includeCursor);
    QTimer::singleShot(0, context, [capture, done = std::move(done)] { done(capture); });
}

#ifdef GUCKSTE_HAVE_DBUS
#include "ScreenCapture.moc"
#endif
