#include "stacktraceprocess.h"
#include "timeprofiler.h"

#include "lz4/lz4.h"

#include <QtEndian>
#include <QTcpSocket>
#include <QTextStream>
#include <QDataStream>
#include <QRegularExpression>
#include <QProcess>
#include <QDebug>

#define BUFFER_SIZE 1048576

StackTraceProcess::StackTraceProcess(QObject* parent)
    : QObject(parent), socket_(new QTcpSocket(this)) {
    buffer_ = new char[BUFFER_SIZE];
    compressBuffer_ = new char[compressBufferSize_];
    socket_->setReadBufferSize(BUFFER_SIZE);
    connect(socket_, &QTcpSocket::readyRead, this, &StackTraceProcess::OnDataReceived);
    connect(socket_, &QTcpSocket::connected, this, &StackTraceProcess::OnConnected);
    connect(socket_, &QTcpSocket::disconnected, this, &StackTraceProcess::OnDisconnected);
}

StackTraceProcess::~StackTraceProcess(){
    delete[] buffer_;
    delete[] compressBuffer_;
}

void StackTraceProcess::ForwardPort(int port) {
    QProcess process;
    QStringList arguments;
    arguments << "forward" << "tcp:" + QString::number(port) << "tcp:7100";
    process.setProgram(execPath_);
    process.setArguments(arguments);
    process.start();
    if (!process.waitForStarted()) {
        emit ConnectionLost();
        return;
    }
    if (!process.waitForFinished()) {
        emit ConnectionLost();
        return;
    }
}

void StackTraceProcess::ConnectToServer(int port) {
    ForwardPort(port);
    connectingServer_ = true;
    socket_->connectToHost("127.0.0.1", static_cast<quint16>(port));
}

void StackTraceProcess::Disconnect() {
    // Aborts the current connection and resets the socket.
    socket_->abort();
    packetSize_ = 0;
}

void StackTraceProcess::Send(const char* data, int length) {
    socket_->write(data, length);
}

void StackTraceProcess::ReadPacket(const QByteArray& bytes) {
    quint32 packetType = *reinterpret_cast<const quint32*>(bytes.data());
    if (packetType == 0) { // stack trace data
        ReadStackTracePacket(bytes);
    } else if (packetType == 1) { // recived command
        CommandHandler(*reinterpret_cast<const quint32*>(bytes.data() + 4));
    } else {
        qDebug() << "Unknown packetType: " << packetType;
    }
}

void StackTraceProcess::ReadStackTracePacket(const QByteArray &bytes) {
    quint32 originSize = *reinterpret_cast<const quint32*>(bytes.data() + 4);
    if (originSize > compressBufferSize_) {
        compressBufferSize_ = static_cast<quint32>(originSize * 1.5f);
        delete[] compressBuffer_;
        compressBuffer_ = new char[compressBufferSize_];
    }
    auto decompressSize = LZ4_decompress_safe(bytes.data() + 8, compressBuffer_, 
        bytes.size() - 8, static_cast<qint32>(compressBufferSize_));
    if (decompressSize == 0) {
        qDebug() << "LZ4 decompression failed!";
        return;
    }
    freeInfo_.clear();
    stackInfo_.clear();
    QByteArray uncompressedBytes = QByteArray::fromRawData(compressBuffer_, decompressSize);
    QDataStream stream(uncompressedBytes);
    stream.setByteOrder(QDataStream::ByteOrder::LittleEndian);
    QByteArray line;
    while (!stream.atEnd()) {
        quint16 lineSize = 0;
        stream >> lineSize;
        line.resize(lineSize);
        if (stream.readRawData(line.data(), lineSize) == -1) {
            qDebug() << "Intepreting data failed!";
            return;
        }
        QDataStream lineStream(line);
        lineStream.setByteOrder(QDataStream::ByteOrder::LittleEndian);
        quint8 type;
        lineStream >> type;
        if (type == static_cast<quint8>(loliFlags::FREE_)) {
            quint32 seq;
            quint64 addr;
            lineStream >> seq >> addr;
            freeInfo_.push_back(qMakePair(seq, addr));
        } else {
            RawStackInfo info;
            lineStream >> info.seq_ >> info.time_ >> info.size_ >> info.addr_ >> info.recType_;
            if (info.recType_ == 0) { // nostack mode
                quint16 strlen = 0;
                lineStream >> strlen;
                QByteArray strBa(strlen, 0);
                if (lineStream.readRawData(strBa.data(), static_cast<qint32>(strlen)) == -1) {
                    qDebug() << "Error reading library name string!";
                    return;
                }
                info.library_ = QString(strBa);
            } else if (info.recType_ == 1) { // stacktrace mode
                quint64 addr;
                while (!lineStream.atEnd()) {
                    lineStream >> addr;
                    info.stacktraces_.push_back(addr);
                }
            } else {
                qDebug() << "Unknown recType!";
                return;
            }
            stackInfo_.push_back(info);
        }
    }
    emit DataReceived();
}

void StackTraceProcess::CommandHandler(quint32 cmd) {
    if (cmd == static_cast<quint32>(loliCommands::SMAPS_DUMP)) {
        emit SMapsDumped();
    }
}

void StackTraceProcess::OnDataReceived() {
    while (socket_->bytesAvailable() > 0) {
        auto size = socket_->read(buffer_, BUFFER_SIZE);
        if (size <= 0)
            return;
        qint64 bufferPos = 0; // pos = size - remains
        qint64 remainBytes = size;
        while (remainBytes > 0) {
            if (packetSize_ == 0) {
                // handles the condiction when buffer's size is less than 4 byte
                // because we need at least 4 byte to interpret the packet's size
                if (bufferCache_.size() + remainBytes < 4) {
                    bufferCache_.append(buffer_ + bufferPos, static_cast<int>(remainBytes));
                    break;
                } else {
                    int remainSize = 4 - bufferCache_.size();
                    if (remainSize > 0) {
                        bufferCache_.append(buffer_ + bufferPos, remainSize);
                        remainBytes -= remainSize;
                    }
                }
                packetSize_ = *reinterpret_cast<quint32*>(bufferCache_.data());
    //            qDebug() << "packetSize_: " << packetSize_;
                bufferPos = size - remainBytes;
                bufferCache_.resize(0);
                if (remainBytes > 0) {
                    if (packetSize_ <= remainBytes) { // the data is stored in the same packet, then read them all
                        bufferCache_.append(buffer_ + bufferPos, static_cast<int>(packetSize_));
                        remainBytes -= packetSize_;
                        bufferPos = size - remainBytes;
                        ReadPacket(bufferCache_);
                        bufferCache_.resize(0);
                        packetSize_ = 0;
                    } else { // the data is splited to another packet, we just append whatever we got
                        bufferCache_.append(buffer_ + bufferPos, static_cast<int>(remainBytes));
                        break;
                    }
                }
            } else {
                auto remainPacketSize = packetSize_ - static_cast<uint>(bufferCache_.size());
                if (remainPacketSize <= remainBytes) { // the remaining data is stored in this packet, read what we need
                    bufferCache_.append(buffer_ + bufferPos, static_cast<int>(remainPacketSize));
                    remainBytes -= remainPacketSize;
                    bufferPos = size - remainBytes;
                    ReadPacket(bufferCache_);
                    bufferCache_.resize(0);
                    packetSize_ = 0;
                } else { // the remaining data is splited to another packet, we just append whatever we got
                    bufferCache_.append(buffer_ + bufferPos, static_cast<int>(remainBytes));
                    break;
                }
            }
        }
    }
}

void StackTraceProcess::OnConnected() {
    connectingServer_ = false;
    serverConnected_ = true;
}

void StackTraceProcess::OnDisconnected() {
    connectingServer_ = false;
    serverConnected_ = false;
    emit ConnectionLost();
}
