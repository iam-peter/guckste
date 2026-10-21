#include "SingleInstance.h"

#include <QDir>
#include <QLocalSocket>

SingleInstance::SingleInstance(QObject *parent)
    : QObject(parent)
{
    connect(&m_server, &QLocalServer::newConnection, this, [this] {
        while (QLocalSocket *socket = m_server.nextPendingConnection()) {
            connect(socket, &QLocalSocket::readyRead, this, [this, socket] {
                while (socket->canReadLine()) {
                    const QString command = QString::fromUtf8(socket->readLine()).trimmed();
                    if (!command.isEmpty())
                        emit commandReceived(command);
                }
            });
            connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        }
    });
}

QString SingleInstance::serverName()
{
    // GUCKSTE_INSTANCE names a separate instance, for trying a build next
    // to the one in use.
    const QString name = qEnvironmentVariable("GUCKSTE_INSTANCE");
    return QStringLiteral("guckste-") + QDir::home().dirName() + (name.isEmpty() ? QString() : u'-' + name);
}

bool SingleInstance::sendToRunning(const QString &command)
{
    QLocalSocket socket;
    socket.connectToServer(serverName());
    if (!socket.waitForConnected(500))
        return false;
    socket.write(command.toUtf8() + '\n');
    socket.waitForBytesWritten(1000);
    socket.disconnectFromServer();
    return true;
}

bool SingleInstance::listen()
{
    // A socket file left by a crashed instance would block the name.
    QLocalServer::removeServer(serverName());
    m_server.setSocketOptions(QLocalServer::UserAccessOption);
    if (!m_server.listen(serverName())) {
        qWarning("Could not listen on %s: %s", qPrintable(serverName()), qPrintable(m_server.errorString()));
        return false;
    }
    return true;
}
