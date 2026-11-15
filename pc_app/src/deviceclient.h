#pragma once

#include <QObject>
#include <QTcpSocket>
#include <QTimer>
#include <QStringList>

// TCP-клиент платы: построчный обмен (LF), таймаут ответа 2 с,
// периодический PING, чтобы плата не считала связь потерянной.
class DeviceClient : public QObject
{
    Q_OBJECT
public:
    explicit DeviceClient(QObject *parent = nullptr);
    ~DeviceClient() override;

    void connectToDevice(const QString &host, quint16 port);
    void disconnectFromDevice();
    bool isConnected() const;

    // Отправить команду. Ответ ждём не дольше kReplyTimeoutMs.
    void send(const QString &line);

    // Во время развёртки плата шлёт сообщения не реже раза в секунду;
    // если их нет kReplyTimeoutMs, выдаётся timeout().
    void setSweepActive(bool active);

    static constexpr int kReplyTimeoutMs = 2000;
    static constexpr int kKeepAliveMs = 3000;

signals:
    void connected();
    void disconnected();
    void errorText(const QString &text);
    void lineReceived(const QString &line, const QStringList &fields);
    void timeout();
    // Для журнала: dir = ">" (ПК -> плата) или "<" (плата -> ПК)
    void exchange(const QString &dir, const QString &line);

private slots:
    void onReadyRead();
    void onKeepAlive();
    void onWatchdog();

private:
    void armWatchdog();
    void sendLine(const QString &line, bool silent);

    QTcpSocket m_sock;
    QByteArray m_rx;
    QTimer m_watchdog;
    QTimer m_keepAlive;
    bool m_waitReply = false;
    bool m_sweep = false;
    bool m_silentPing = false;
};
