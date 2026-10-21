#pragma once

#include <QImage>
#include <QObject>
#include <QRect>

#include <functional>

// A shot of all screens as one image, with the virtual desktop rectangle it
// covers in logical coordinates.
struct Capture
{
    QImage image;
    QRect geometry;
    bool ok = false;
};

namespace ScreenCapture {

// Grabs every screen through Qt, which works on X11, Windows and macOS,
// the pointer painted in when asked.
Capture grabScreens(bool includeCursor);

// Grabs the screens the way the platform allows: through Qt, or on Wayland
// by asking the desktop portal, which answers later. `done` is called once
// either way, on the main thread.
void grab(QObject *context, bool includeCursor, std::function<void(const Capture &)> done);

} // namespace ScreenCapture
