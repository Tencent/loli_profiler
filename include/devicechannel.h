#ifndef DEVICECHANNEL_H
#define DEVICECHANNEL_H

#include <cstdint>
#include <functional>
#include <string>

// Qt-free TCP channel to the injected in-device stacktrace server
// (replaces QTcpSocket usage in StackTraceProcess). Transport only - the
// packet framing (packet-size prefix, LZ4 payload) and protocol handling
// stay in StackTraceProcess, byte-identical to the Qt original.
//
// Built on SFML's sf::TcpSocket (thirdparty/SFML, network module). The
// socket is non-blocking: reads are pumped by the consumer's loop (GUI
// frame loop / CLI loop), mirroring how StackTraceProcess previously
// drained the QTcpSocket on readyRead.
//
// State model (mirrors QTcpSocket::socketState as used by the original):
//  Disconnected -> Connecting -> Connected -> Disconnected (on error/abort).
// Callbacks fire from Pump() / Connect() - never from a background thread.
class DeviceChannel {
public:
    enum class State {
        Disconnected,
        Connecting,
        Connected,
    };

    // Called once the connection is established (or immediately with
    // success=false if the connect attempt fails).
    using ConnectedHandler = std::function<void(bool success)>;
    // Called when an established connection is lost (remote close, error,
    // or Abort()). NOT called for a failed initial connect attempt.
    using DisconnectedHandler = std::function<void()>;
    // Data availability: fired from Pump() when new bytes arrived. The
    // consumer then calls Read() until it returns 0 (QTcpSocket readyRead
    // + readAll loop parity).
    using DataHandler = std::function<void()>;

    DeviceChannel() = default;
    ~DeviceChannel();

    DeviceChannel(const DeviceChannel&) = delete;
    DeviceChannel& operator=(const DeviceChannel&) = delete;

    void SetConnectedHandler(ConnectedHandler handler) { onConnected_ = std::move(handler); }
    void SetDisconnectedHandler(DisconnectedHandler handler) { onDisconnected_ = std::move(handler); }
    void SetDataHandler(DataHandler handler) { onData_ = std::move(handler); }

    // Begins an asynchronous connection to host:port (e.g. "127.0.0.1" and
    // the adb-forwarded port). Connection attempts are polled inside
    // Pump(): once it settles, the connected handler fires (from Pump, not
    // from a background thread). A short connect timeout aborts the attempt
    // and reports failure, matching the observable behavior of a
    // QTcpSocket whose connectToHost never completes.
    void ConnectAsync(const std::string& host, uint16_t port);

    // True while a ConnectAsync attempt is in progress.
    bool IsConnecting() const { return state_ == State::Connecting; }

    // True once connected and not yet disconnected/aborted.
    bool IsConnected() const { return state_ == State::Connected; }

    // Drives all pending I/O: settles in-flight connects, delivers data
    // notifications, detects remote disconnects. Must be called regularly
    // by the consumer's loop (once per GUI frame / CLI loop iteration).
    // All handlers fire from within Pump(), on the caller's thread.
    void Pump();

    // Reads up to maxSize bytes into buffer. Returns the number of bytes
    // read (0 when nothing is available). Mirrors QTcpSocket::read.
    std::size_t Read(char* buffer, std::size_t maxSize);

    // Bytes currently buffered and readable (mirrors QTcpSocket::bytesAvailable).
    std::size_t BytesAvailable() const;

    // Sends raw bytes. Returns true when all bytes were sent. Mirrors
    // QTcpSocket::write (blocking-style full send).
    bool Send(const char* data, std::size_t length);

    // Aborts the connection and resets the channel (mirrors
    // QTcpSocket::abort). Does not fire the disconnected handler (matches
    // the Qt original, where a user-initiated abort is not a ConnectionLost
    // event).
    void Abort();

private:
    struct Impl;
    Impl* impl_ = nullptr;

    State state_ = State::Disconnected;

    ConnectedHandler onConnected_;
    DisconnectedHandler onDisconnected_;
    DataHandler onData_;
};

#endif // DEVICECHANNEL_H
