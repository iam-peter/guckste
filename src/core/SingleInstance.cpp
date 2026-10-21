#include "SingleInstance.h"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QDir>
#include <QLocalSocket>
#include <QStandardPaths>
#include <QThread>

SingleInstance::SingleInstance(QObject *parent)
    : QObject(parent)
{
    connect(&m_server, &QLocalServer::newConnection, this, [this] {
        while (QLocalSocket *socket = m_server.nextPendingConnection()) {
            connect(socket, &QLocalSocket::readyRead, this, [this, socket] {
                while (socket->canReadLine()) {
                    const QString command = QString::fromUtf8(socket->readLine()).trimmed();
                    if (command.isEmpty())
                        continue;
                    // The sender waits for this before it quits.
                    socket->write("ok\n");
                    socket->flush();
                    emit commandReceived(command);
                }
            });
            connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        }
    });
}

SingleInstance::~SingleInstance()
{
    release();
}

QString SingleInstance::serverName()
{
    // GUCKSTE_INSTANCE names a separate instance, for trying a build next
    // to the one in use.
    const QString name = qEnvironmentVariable("GUCKSTE_INSTANCE");
    return QStringLiteral("guckste-") + QDir::home().dirName() + (name.isEmpty() ? QString() : u'-' + name);
}

QString SingleInstance::lockPath()
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (dir.isEmpty())
        dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    return dir + u'/' + serverName() + QStringLiteral(".lock");
}

bool SingleInstance::claim()
{
    if (m_lock)
        return true;
    if (isRunning())
        return false;
    auto lock = std::make_unique<QLockFile>(lockPath());
    // A lock whose process is gone is stale and removed by tryLock.
    lock->setStaleLockTime(0);
    if (!lock->tryLock(0))
        return false;
    m_lock = std::move(lock);
    return true;
}

bool SingleInstance::listen()
{
    // A socket file left by a crashed instance would block the name; the
    // lock says nobody is listening on it.
    QLocalServer::removeServer(serverName());
    m_server.setSocketOptions(QLocalServer::UserAccessOption);
    if (!m_server.listen(serverName())) {
        qWarning("Could not listen on %s: %s", qPrintable(serverName()), qPrintable(m_server.errorString()));
        return false;
    }
    return true;
}

void SingleInstance::release()
{
    m_server.close();
    if (m_lock) {
        m_lock->unlock();
        m_lock.reset();
    }
}

bool SingleInstance::isRunning()
{
    QLockFile lock(lockPath());
    lock.setStaleLockTime(0);
    if (!lock.tryLock(0))
        return true;
    lock.unlock();
    // An instance from before the lock file only has its socket.
    QLocalSocket socket;
    socket.connectToServer(serverName());
    return socket.waitForConnected(100);
}

bool SingleInstance::sendToRunning(const QString &command, int timeoutMs)
{
    if (!isRunning())
        return false;
    // Between its lock and its server there is a moment to wait for.
    QDeadlineTimer deadline(timeoutMs);
    while (!deadline.hasExpired()) {
        QLocalSocket socket;
        socket.connectToServer(serverName());
        if (socket.waitForConnected(200)) {
            socket.write(command.toUtf8() + '\n');
            socket.flush();
            // Acknowledged, or at least taken.
            if (socket.waitForReadyRead(qMax(200, int(deadline.remainingTime()))) || socket.bytesToWrite() == 0)
                return true;
            return true;
        }
        QThread::msleep(100);
        if (!isRunning())
            return false;
    }
    qWarning("Guckste is running but not answering");
    return true;
}

bool SingleInstance::quitRunning(int timeoutMs)
{
    if (!isRunning())
        return true;
    sendToRunning(QStringLiteral("quit"), timeoutMs);
    QDeadlineTimer deadline(timeoutMs);
    while (isRunning() && !deadline.hasExpired())
        QThread::msleep(50);
    return !isRunning();
}
