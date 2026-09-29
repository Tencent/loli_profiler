#ifndef STACKTRACECHANNEL_H
#define STACKTRACECHANNEL_H

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "devicechannel.h"
#include "loliuuid.h"

// Qt-free stacktrace channel (replaces StackTraceProcess, task 4.3).
//
// Owns the full live-capture data path:
//  - adb forward tcp:<port> tcp:7100 (via ProcessRunner, blocking one-shot)
//  - DeviceChannel TCP connection to 127.0.0.1:<port> (the forwarded port)
//  - packet framing: quint32 payload size, then payload
//  - packet types: 0 = stacktrace data (LZ4-compressed line records),
//                  1 = command ack (quint32 cmd)
//  - interpretation of the decompressed line stream (see below)
//
// Wire format of a stacktrace packet payload (after 4-byte packetType and
// 4-byte originSize prefixes, payload LZ4-compressed; all LITTLE-endian
// inside, unlike the big-endian .loli file):
//   repeated: quint16 lineSize | line[lineSize]
//   line: quint8 recType
//     recType 0 (FREE_):  quint32 seq, quint64 addr
//     recType 1 (MALLOC_/CALLOC_/...): quint32 seq, qint64 time,
//                quint32 size, quint64 addr, then either
//                recType 1 (nostack): quint16 strlen + raw bytes (library)
//                recType 2 (stacktrace): repeated quint64 frame addrs
//   NOTE the original code keys the record variant on the SECOND byte
//   (info.recType_ after seq/time/size/addr): 0 = nostack (library string),
//   1 = stacktrace (frames) - see StackTraceProcess::ReadStackTracePacket.
//
// Threading model (QTcpSocket parity):
//  - The consumer pumps Pump() on its own loop (GUI frame loop / CLI loop).
//  - Pump() drains the socket, reassembles packets, interprets payloads,
//    and fires the data callback with the accumulated results.
//  - Callbacks fire from Pump(), never from background threads.
class StacktraceChannel {
public:
    enum class LoliFlag : uint8_t {
        Free = 0,
        Malloc = 1,
        Calloc = 2,
        Memalign = 3,
        Realloc = 4,
    };
    enum class LoliCommand : uint8_t {
        SmapsDump = 0,
    };

    // One interpreted allocation record (mirrors RawStackInfo).
    struct RawStackInfo {
        uint32_t seq = 0;
        int64_t time = 0;
        uint32_t size = 0;
        uint64_t addr = 0;
        uint8_t recType = 0;
        std::string library;        // nostack mode: raw library name
        uint32_t libraryHash = 0;   // stack mode: interned hashcode (always 0 in the stream)
        std::vector<uint64_t> stacktraces; // stack mode: frame addresses (leaf-first)
    };
    using FreeInfo = std::pair<uint32_t, uint64_t>; // (seq, addr)

    // Fired from Pump() after a complete stacktrace packet was interpreted.
    using DataHandler = std::function<void(const std::vector<RawStackInfo>& stacks,
                                           const std::vector<FreeInfo>& frees)>;
    // Fired from Pump() when the established connection drops.
    using ConnectionLostHandler = std::function<void()>;
    // Fired from Pump() when the device acks a command (e.g. SmapsDump).
    using SmapsDumpedHandler = std::function<void()>;

    StacktraceChannel();
    ~StacktraceChannel();

    StacktraceChannel(const StacktraceChannel&) = delete;
    StacktraceChannel& operator=(const StacktraceChannel&) = delete;

    void SetAdbPath(const std::string& path) { adbPath_ = path; }
    void SetDeviceSerial(const std::string& serial) { deviceSerial_ = serial; }

    void SetDataHandler(DataHandler handler) { onData_ = std::move(handler); }
    void SetConnectionLostHandler(ConnectionLostHandler handler) {
        onConnectionLost_ = std::move(handler);
    }
    void SetSmapsDumpedHandler(SmapsDumpedHandler handler) {
        onSmapsDumped_ = std::move(handler);
    }

    // adb forward tcp:<port> tcp:7100, then start the async connect to
    // 127.0.0.1:<port>. Connection establishment is polled in Pump(); the
    // Qt original emitted ConnectionLost if the forward failed (returned
    // false) - here a failed forward reports via the connection-lost
    // handler on the next Pump().
    void ConnectToServer(uint16_t port);

    // Direct TCP connect without the adb forward step (loopback tests,
    // local servers, or a pre-forwarded port). Same pump semantics.
    void ConnectDirect(const std::string& host, uint16_t port);

    // True while the TCP connect attempt is in flight.
    bool IsConnecting() const { return channel_.IsConnecting(); }
    bool IsConnected() const { return channel_.IsConnected(); }

    // Sends raw bytes to the device (QTcpSocket::write parity).
    void Send(const char* data, size_t length);

    // Drives all pending I/O. Call once per consumer loop iteration.
    void Pump();

    // Aborts the connection and resets the packet reassembly state.
    void Disconnect();

private:
    void OnSocketData();
    void InterpretPayload(const uint8_t* data, size_t size);

    std::string adbPath_;
    std::string deviceSerial_;

    DeviceChannel channel_;
    DataHandler onData_;
    ConnectionLostHandler onConnectionLost_;
    SmapsDumpedHandler onSmapsDumped_;

    // Packet reassembly (mirrors StackTraceProcess's bufferCache_/packetSize_).
    std::string bufferCache_;
    uint32_t packetSize_ = 0;

    // Decompression buffer (grows as needed).
    std::vector<char> compressBuffer_;

    // Interpreted results handed to the data callback.
    std::vector<RawStackInfo> stackInfo_;
    std::vector<FreeInfo> freeInfo_;

    bool forwardFailed_ = false;
};

#endif // STACKTRACECHANNEL_H
