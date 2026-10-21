#pragma once

#include <QObject>
#include <QThread>

#include <atomic>

// Every key pressed anywhere on the desktop, for showing typed keys in a
// recording: raw key events from XInput2 on X11, a low-level keyboard
// hook on Windows, an event tap on macOS (with the Accessibility
// permission). Wayland has nothing to listen with, and supported() says.
class KeyListener : public QThread
{
    Q_OBJECT

public:
    explicit KeyListener(QObject *parent = nullptr);
    ~KeyListener() override;

    static bool supported();
    // What is missing where supported() is true but begin() fails.
    static QString requirement();
    bool begin();
    void end();

    // "Ctrl+Shift+S", "A", "Enter", "⌫": what a key press reads as.
    static QString label(const QString &keyName, bool shift, bool ctrl, bool alt, bool super);

signals:
    void keyPressed(const QString &label);

protected:
    void run() override;

private:
    std::atomic<bool> m_stop{false};
};
