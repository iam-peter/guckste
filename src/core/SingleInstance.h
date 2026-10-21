#pragma once

#include <QLocalServer>
#include <QObject>

// One Guckste per user: later starts hand their command line to the first
// over a local socket and quit.
class SingleInstance : public QObject
{
    Q_OBJECT

public:
    explicit SingleInstance(QObject *parent = nullptr);

    // Delivers a command to a running instance; false if there is none.
    static bool sendToRunning(const QString &command);
    // Becomes the running instance.
    bool listen();

signals:
    void commandReceived(const QString &command);

private:
    static QString serverName();

    QLocalServer m_server;
};
