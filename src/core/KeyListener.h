#pragma once

#include <QObject>
#include <QThread>

#include <atomic>

// Every key pressed anywhere on the desktop, for showing typed keys in a
// recording. On X11 the XInput2 extension reports raw key events from the
// root window; elsewhere there is nothing to listen with and start()
// returns false.
class KeyListener : public QThread
{
    Q_OBJECT

public:
    explicit KeyListener(QObject *parent = nullptr);
    ~KeyListener() override;

    static bool supported();
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
