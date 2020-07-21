#ifndef STACKTRACEPROCESS_H
#define STACKTRACEPROCESS_H

#include <QObject>
#include <QVector>

enum class loliFlags : quint8 {
    FREE_ = 0,
    MALLOC_ = 1,
    CALLOC_ = 2,
    MEMALIGN_ = 3,
    REALLOC_ = 4,
    COMMAND_ = 255,
};

enum class loliCommands : quint8 {
    SMAPS_DUMP = 0,
};

struct RawStackInfo {
    quint32 seq_;
    qint64 time_;
    quint32 size_;
    quint64 addr_;
    quint8 recType_;
    QString library_;
    QVector<quint64> stacktraces_;
};

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
    void Send(const char* data, int length);

    const QVector<RawStackInfo>& GetStackInfo() const { return stackInfo_; }
    const QVector<QPair<quint32, quint64>>& GetFreeInfo() const { return freeInfo_; }

    void SetExecutablePath(const QString& str) { execPath_ = str; }
    const QString& GetExecutablePath() const { return execPath_; }

signals:
    void DataReceived();
    void ConnectionLost();
    void SMapsDumped();

private:
    void Interpret(const QByteArray& bytes);
    void CommandHandler(int cmd);
    void OnDataReceived();
    void OnConnected();
    void OnDisconnected();

private:
    QString execPath_;
    QVector<RawStackInfo> stackInfo_;
    QVector<QPair<quint32, quint64>> freeInfo_;
    QTcpSocket* socket_ = nullptr;
    bool connectingServer_ = false;
    bool serverConnected_ = false;
    quint32 packetSize_ = 0;
    char* buffer_ = nullptr;
    char* compressBuffer_ = nullptr;
    quint32 compressBufferSize_ = 1024;
    QByteArray bufferCache_;
};

#endif // STACKTRACEPROCESS_H
