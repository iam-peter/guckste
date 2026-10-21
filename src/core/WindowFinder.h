#pragma once

#include <QList>
#include <QRect>

class QWindow;

// The windows on screen, frames included, front to back, in logical screen
// coordinates: for picking one in the overlay. Taken once when the overlay
// opens, since nothing moves while it is up.
namespace WindowFinder {

QList<QRect> windows();

// Gives `window` the keyboard focus, even over a newer focus change by
// another client: for the overlay, which bypasses the window manager.
void takeFocus(QWindow *window);

} // namespace WindowFinder
