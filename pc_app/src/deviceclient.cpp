#include "deviceclient.h"

DeviceClient::DeviceClient(QObject *parent) : QObject(parent)
{
    m_watchdog.setSingleShot(true);
    m_watchdog.setInterval(kReplyTimeoutMs);
    connect(&m_watchdog, &QTimer::timeout, this, &DeviceClient::onWatchdog);

    m_keepAlive.setInterval(kKeepAliveMs);
    connect(&m_keepAlive, &QTimer::timeout, this, &DeviceClient::onKeepAlive);

    connect(&m_sock, &QTcpSocket::connected, this, [this] {
        m_rx.clear();
        m_waitReply = false;
        m_keepAlive.start();
        emit connected();
    });
    connect(&m_sock, &QTcpSocket::disconnected, this, [this] {
        m_keepAlive.stop();
        m_watchdog.stop();
        m_waitReply = false;
        emit disconnected();
    });
    connect(&m_sock, &QTcpSocket::readyRead, this, &DeviceClient::onReadyRead);
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    connect(&m_sock, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
#else
    connect(&m_sock, QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::error), this,
            [this](QAbstractSocket::SocketError) {
#endif
        emit errorText(m_sock.errorString());
    });
}

DeviceClient::~DeviceClient()
{
    // При разрушении сокет может сообщить об отключении: к этому моменту
    // таймеры уже удалены, поэтому сначала отключаем все сигналы.
    m_sock.disconnect(this);
    disconnect();
    m_sock.abort();
}

void DeviceClient::connectToDevice(const QString &host, quint16 port)
{
    m_sock.abort();
    m_sock.connectToHost(host, port);
}

void DeviceClient::disconnectFromDevice()
{
    m_sock.disconnectFromHost();
}

bool DeviceClient::isConnected() const
{
    return m_sock.state() == QAbstractSocket::ConnectedState;
}

void DeviceClient::send(const QString &line)
{
    sendLine(line, false);
}

void DeviceClient::sendLine(const QString &line, bool silent)
{
    if (!isConnected())
        return;
    m_sock.write(line.toLatin1() + '\n');
    if (!silent)
        emit exchange(">", line);
    m_waitReply = true;
    armWatchdog();
    m_keepAlive.start(); // перезапуск: PING нужен только при молчании ПК
}

void DeviceClient::setSweepActive(bool active)
{
    m_sweep = active;
    if (active)
        armWatchdog();
    else if (!m_waitReply)
        m_watchdog.stop();
}

void DeviceClient::armWatchdog()
{
    m_watchdog.start();
}

void DeviceClient::onReadyRead()
{
    m_rx += m_sock.readAll();
    int pos;
    while ((pos = m_rx.indexOf('\n')) >= 0) {
        QString line = QString::fromLatin1(m_rx.left(pos)).trimmed();
        m_rx.remove(0, pos + 1);
        if (line.isEmpty())
            continue;
        m_waitReply = false;
        if (m_sweep)
            armWatchdog();
        else
            m_watchdog.stop();
        // Ответы на автоматический PING в журнал не пишем
        if (line != "PONG" || !m_silentPing)
            emit exchange("<", line);
        if (line == "PONG" && m_silentPing) {
            m_silentPing = false;
            continue;
        }
        emit lineReceived(line, line.split(';'));
    }
    if (m_rx.size() > 65536) // защита от мусора без LF
        m_rx.clear();
}

void DeviceClient::onKeepAlive()
{
    if (!isConnected() || m_waitReply)
        return;
    m_silentPing = true;
    sendLine("PING", true);
}

void DeviceClient::onWatchdog()
{
    m_waitReply = false;
    m_silentPing = false;
    emit timeout();
    if (m_sweep)
        armWatchdog();
}
