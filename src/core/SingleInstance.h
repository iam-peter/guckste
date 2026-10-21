#pragma once

#include <QLocalServer>
#include <QLockFile>
#include <QObject>

#include <memory>

// One Guckste per user. The running one holds a lock file; a later start
// hands its command over a local socket to it and quits. The lock is what
// decides, so a stale socket or two starts at once cannot make a second
// instance; a lock left by a crash is stale and taken over.
class SingleInstance : public QObject
{
    Q_OBJECT

public:
    explicit SingleInstance(QObject *parent = nullptr);
    ~SingleInstance() override;

    // Takes the lock, or false if another instance holds it.
    bool claim();
    // Starts the server, after claim().
    bool listen();
    // Gives the lock up, for handing over to a replacement.
    void release();

    // Delivers a command to the running instance, waiting a moment for one
    // that is still starting up; false if none took it.
    static bool sendToRunning(const QString &command, int timeoutMs = 3000);
    // Asks the running instance to quit and waits until it is gone.
    static bool quitRunning(int timeoutMs = 5000);
    static bool isRunning();

signals:
    void commandReceived(const QString &command);

private:
    static QString serverName();
    static QString lockPath();

    QLocalServer m_server;
    std::unique_ptr<QLockFile> m_lock;
};
