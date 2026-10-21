#pragma once

#include <QList>
#include <QRect>

// The windows on screen, frames included, front to back, in logical screen
// coordinates: for picking one in the overlay. Taken once when the overlay
// opens, since nothing moves while it is up.
namespace WindowFinder {

QList<QRect> windows();

} // namespace WindowFinder
