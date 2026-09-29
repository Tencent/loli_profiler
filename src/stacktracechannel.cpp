#include "stacktracechannel.h"

#include <cstring>
#include <memory>

#include "lz4/lz4.h"
#include "processrunner.h"

namespace {

// Little-endian readers for the in-packet wire format (QDataStream with
// ByteOrder::LittleEndian in the Qt original - the ONLY little-endian part
// of the whole protocol).
uint16_t ReadU16LE(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}
uint32_t ReadU32LE(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
uint64_t ReadU64LE(const uint8_t* p) {
    return static_cast<uint64_t>(ReadU32LE(p)) |
           (static_cast<uint64_t>(ReadU32LE(p + 4)) << 32);
}

// Blocking one-shot adb invocation (AdbProcess::SetArguments + StartProcess
// parity: returns false when the adb command fails to run/finish).
bool RunAdb(const std::string& adbPath, const std::string& deviceSerial,
            const std::vector<std::string>& extraArgs, std::string* errorOut = nullptr) {
    ProcessRunner r;
    r.SetProgram(adbPath);
    std::vector<std::string> args;
    if (!deviceSerial.empty())
        args.push_back("-s"), args.push_back(deviceSerial);
    args.insert(args.end(), extraArgs.begin(), extraArgs.end());
    r.SetArguments(args);
    if (!r.Start()) {
        if (errorOut)
            *errorOut = "adb failed to start";
        return false;
    }
    if (!r.WaitForFinished(15000)) {
        r.Kill();
        if (errorOut)
            *errorOut = "adb timed out";
        return false;
    }
    return true;
}

} // namespace

StacktraceChannel::StacktraceChannel() {
    compressBuffer_.resize(1024);
    channel_.SetDataHandler([this]() { OnSocketData(); });
    channel_.SetDisconnectedHandler([this]() {
        if (onConnectionLost_)
            onConnectionLost_();
    });
}

StacktraceChannel::~StacktraceChannel() {
    Disconnect();
}

void StacktraceChannel::ConnectToServer(uint16_t port) {
    // adb forward tcp:<port> tcp:7100 (blocking one-shot, like the Qt
    // original's synchronous ForwardPort).
    if (!RunAdb(adbPath_, deviceSerial_,
                 {"forward", "tcp:" + std::to_string(port), "tcp:7100"})) {
        forwardFailed_ = true;
        return;
    }
    channel_.ConnectAsync("127.0.0.1", port);
}

void StacktraceChannel::ConnectDirect(const std::string& host, uint16_t port) {
    channel_.ConnectAsync(host, port);
}

void StacktraceChannel::Send(const char* data, size_t length) {
    channel_.Send(data, length);
}

void StacktraceChannel::Disconnect() {
    channel_.Abort();
    packetSize_ = 0;
    bufferCache_.clear();
}

void StacktraceChannel::Pump() {
    if (forwardFailed_) {
        // The Qt original emitted ConnectionLost when the adb forward
        // failed; surface it once on the pump.
        forwardFailed_ = false;
        if (onConnectionLost_)
            onConnectionLost_();
        return;
    }
    channel_.Pump();
}

void StacktraceChannel::OnSocketData() {
    // Drain the channel's rx buffer into the packet reassembly state
    // (mirrors StackTraceProcess::OnDataReceived: 1MB chunks, 4-byte size
    // prefixes, split packets appended to bufferCache_).
    std::vector<char> chunk(1 << 20);
    for (;;) {
        const size_t got = channel_.Read(chunk.data(), chunk.size());
        if (got == 0)
            break;
        const char* buffer = chunk.data();
        size_t bufferPos = 0;
        size_t remainBytes = got;
        while (remainBytes > 0) {
            if (packetSize_ == 0) {
                // Need at least 4 bytes to interpret the packet size.
                if (bufferCache_.size() + remainBytes < 4) {
                    bufferCache_.append(buffer + bufferPos, remainBytes);
                    break;
                }
                const size_t remainSize = 4 - bufferCache_.size();
                if (remainSize > 0) {
                    bufferCache_.append(buffer + bufferPos, remainSize);
                    remainBytes -= remainSize;
                }
                packetSize_ = ReadU32LE(reinterpret_cast<const uint8_t*>(bufferCache_.data()));
                bufferPos = got - remainBytes;
                bufferCache_.clear();
                if (remainBytes > 0) {
                    if (packetSize_ <= remainBytes) {
                        // Whole packet in this chunk.
                        bufferCache_.assign(buffer + bufferPos, packetSize_);
                        remainBytes -= packetSize_;
                        bufferPos = got - remainBytes;
                        InterpretPayload(reinterpret_cast<const uint8_t*>(bufferCache_.data()),
                                         bufferCache_.size());
                        bufferCache_.clear();
                        packetSize_ = 0;
                    } else {
                        // Split across chunks: keep what we got.
                        bufferCache_.assign(buffer + bufferPos, remainBytes);
                        break;
                    }
                }
            } else {
                const size_t remainPacketSize = packetSize_ - bufferCache_.size();
                if (remainPacketSize <= remainBytes) {
                    bufferCache_.append(buffer + bufferPos, remainPacketSize);
                    remainBytes -= remainPacketSize;
                    bufferPos = got - remainBytes;
                    InterpretPayload(reinterpret_cast<const uint8_t*>(bufferCache_.data()),
                                     bufferCache_.size());
                    bufferCache_.clear();
                    packetSize_ = 0;
                } else {
                    bufferCache_.append(buffer + bufferPos, remainBytes);
                    break;
                }
            }
        }
    }
}

void StacktraceChannel::InterpretPayload(const uint8_t* bytes, size_t size) {
    if (size < 4)
        return;
    const uint32_t packetType = ReadU32LE(bytes);
    if (packetType == 1) { // received command
        if (size >= 8) {
            const uint32_t cmd = ReadU32LE(bytes + 4);
            if (cmd == static_cast<uint32_t>(LoliCommand::SmapsDump) && onSmapsDumped_)
                onSmapsDumped_();
        }
        return;
    }
    if (packetType != 0)
        return; // unknown packet type (Qt original logged and dropped)

    // Stacktrace data: LZ4-compressed line records.
    if (size < 8)
        return;
    const uint32_t originSize = ReadU32LE(bytes + 4);
    if (originSize > compressBuffer_.size())
        compressBuffer_.resize(static_cast<size_t>(originSize * 1.5f));
    const int decompressSize =
        LZ4_decompress_safe(reinterpret_cast<const char*>(bytes + 8), compressBuffer_.data(),
                            static_cast<int>(size - 8),
                            static_cast<int>(compressBuffer_.size()));
    if (decompressSize == 0)
        return;

    const uint8_t* data = reinterpret_cast<const uint8_t*>(compressBuffer_.data());
    size_t pos = 0;
    freeInfo_.clear();
    stackInfo_.clear();
    while (pos + 2 <= static_cast<size_t>(decompressSize)) {
        const uint16_t lineSize = ReadU16LE(data + pos);
        pos += 2;
        if (pos + lineSize > static_cast<size_t>(decompressSize))
            break; // malformed tail (Qt original returned early)
        const uint8_t* line = data + pos;
        const size_t lineEnd = lineSize;
        pos += lineSize;

        if (lineSize < 1)
            continue;
        const uint8_t type = line[0];
        if (type == static_cast<uint8_t>(LoliFlag::Free)) {
            if (lineEnd < 1 + 12)
                continue;
            freeInfo_.emplace_back(ReadU32LE(line + 1), ReadU64LE(line + 5));
        } else {
            // seq(4) time(8) size(4) addr(8) recType(1) then payload
            if (lineEnd < 1 + 4 + 8 + 4 + 8 + 1)
                continue;
            RawStackInfo info;
            info.seq = ReadU32LE(line + 1);
            info.time = static_cast<int64_t>(ReadU64LE(line + 5));
            info.size = ReadU32LE(line + 13);
            info.addr = ReadU64LE(line + 17);
            info.recType = line[25];
            const uint8_t* payload = line + 26;
            const size_t payloadSize = lineEnd - 26;
            if (info.recType == 0) { // nostack mode: library string
                if (payloadSize < 2)
                    continue;
                const uint16_t strlen = ReadU16LE(payload);
                if (payloadSize < 2 + strlen)
                    continue;
                info.library.assign(reinterpret_cast<const char*>(payload + 2), strlen);
            } else if (info.recType == 1) { // stacktrace mode: frame addrs
                const size_t frameCount = payloadSize / 8;
                info.stacktraces.reserve(frameCount);
                for (size_t f = 0; f < frameCount; ++f)
                    info.stacktraces.push_back(ReadU64LE(payload + f * 8));
            } else {
                continue; // unknown recType
            }
            stackInfo_.push_back(std::move(info));
        }
    }
    if (onData_)
        onData_(stackInfo_, freeInfo_);
}
