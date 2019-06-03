#ifndef STACKTRACEPROCESS_H
#define STACKTRACEPROCESS_H

#include <QObject>
#include <QVector>

class QTcpSocket;
class StackTraceProcess : public QObject {
    Q_OBJECT
public:
    StackTraceProcess(QObject* parent = nullptr);
    ~StackTraceProcess() override;

    void ConnectToServer(int port);
    void Disconnect();
    bool IsConnecting() const { return connectingServer_; }
    bool IsConnected() const { return serverConnected_; }

    const QVector<QStringList>& GetStackInfo() const { return stackInfo_; }
    const QVector<QPair<int, QString>>& GetFreeInfo() const { return freeInfo_; }

    void SetExecutablePath(const QString& str) { execPath_ = str; }
    const QString& GetExecutablePath() const { return execPath_; }

signals:
    void DataReceived();
    void ConnectionLost();

private:
    void Interpret(const QByteArray& bytes);
    void OnDataReceived();
    void OnConnected();
    void OnDisconnected();

private:
    QString execPath_;
    QVector<QStringList> stackInfo_;
    QVector<QPair<int, QString>> freeInfo_;
    QTcpSocket* socket_;
    bool connectingServer_ = false;
    bool serverConnected_ = false;
    quint32 packetSize_ = 0;
    char* buffer_ = nullptr;
    QByteArray bufferCache_;
};

#endif // STACKTRACEPROCESS_H
