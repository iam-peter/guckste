#pragma once

#include <QObject>
#include <QThread>

#include <atomic>

// Every key pressed anywhere on the desktop, for showing typed keys in a
// recording, and the mouse buttons when asked, for its stills: raw events
// from XInput2 on X11, low-level hooks on Windows, an event tap on macOS
// (with the Accessibility permission). Wayland has nothing to listen
// with, and supported() says.
class KeyListener : public QThread
{
    Q_OBJECT

public:
    explicit KeyListener(QObject *parent = nullptr);
    ~KeyListener() override;

    static bool supported();
    // What is missing where supported() is true but begin() fails.
    static QString requirement();
    // Mouse buttons too, from the next begin() on.
    void setListenToButtons(bool on) { m_buttons = on; }
    bool begin();
    void end();

    // "Ctrl+Shift+S", "A", "Enter", "⌫": what a key press reads as.
    static QString label(const QString &keyName, bool shift, bool ctrl, bool alt, bool super);

signals:
    void keyPressed(const QString &label);
    // The left, middle or right button; the wheel is left out.
    void buttonPressed();

protected:
    void run() override;

private:
    std::atomic<bool> m_stop{false};
    bool m_buttons = false;
};
