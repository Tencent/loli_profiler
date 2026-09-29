#include "devicechannel.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <thread>

#include <SFML/Network/IpAddress.hpp>
#include <SFML/Network/Socket.hpp>
#include <SFML/Network/TcpSocket.hpp>

namespace {

// How long a connect attempt may take before it is treated as failed
// (mirrors the effective behavior of a QTcpSocket connectToHost that never
// completes - the Qt event loop would eventually error out).
constexpr uint64_t kConnectTimeoutMs = 10000;

} // namespace

struct DeviceChannel::Impl {
    std::unique_ptr<sf::TcpSocket> socket; // null until connect settles
    std::string rxBuffer;                  // received but not yet consumed by Read()
    std::thread connectThread;             // runs the blocking connect attempt
    // 0 = attempt in flight, 1 = connected, 2 = failed/refused/timeout.
    std::atomic<int> connectResult{0};
};

DeviceChannel::~DeviceChannel() {
    Abort();
    delete impl_;
}

void DeviceChannel::ConnectAsync(const std::string& host, uint16_t port) {
    Abort();
    if (!impl_)
        impl_ = new Impl();

    const std::optional<sf::IpAddress> address = sf::IpAddress::resolve(host);
    if (!address) {
        state_ = State::Disconnected;
        if (onConnected_)
            onConnected_(false);
        return;
    }

    impl_->socket = std::make_unique<sf::TcpSocket>();
    impl_->connectResult = 0;
    state_ = State::Connecting;

    // The connect attempt runs on a watcher thread using SFML's blocking
    // connect-with-timeout: its internal select() correctly distinguishes
    // established / refused / timed-out, which non-blocking-socket polling
    // cannot (a refused socket never resolves a peer address). Pump()
    // settles the result on the consumer's thread.
    impl_->connectThread = std::thread([this, address = *address, port]() {
        const sf::Socket::Status status =
            impl_->socket->connect(address, port, sf::milliseconds(kConnectTimeoutMs));
        impl_->connectResult.store(
            status == sf::Socket::Status::Done ? 1 : 2, std::memory_order_release);
    });
}

void DeviceChannel::Pump() {
    if (state_ == State::Connecting) {
        const int result = impl_->connectResult.load(std::memory_order_acquire);
        if (result == 0)
            return; // attempt still in flight

        if (impl_->connectThread.joinable())
            impl_->connectThread.join();

        if (result == 1) {
            impl_->socket->setBlocking(false); // data path is poll-driven
            state_ = State::Connected;
            if (onConnected_)
                onConnected_(true);
        } else {
            impl_->socket.reset();
            state_ = State::Disconnected;
            if (onConnected_)
                onConnected_(false);
        }
        return;
    }

    if (state_ != State::Connected)
        return;

    // Process data in bounded batches. Buffering the entire available stream
    // before invoking onData_ lets a busy Android producer grow this buffer
    // faster than the consumer can parse it.
    char chunk[64 * 1024];
    std::size_t received = 0;
    bool gotData = false;
    bool connectionLost = false;
    std::size_t batchBytes = 0;
    for (;;) {
        const sf::Socket::Status status =
            impl_->socket->receive(chunk, sizeof(chunk), received);
        if (status == sf::Socket::Status::Done) {
            impl_->rxBuffer.insert(impl_->rxBuffer.end(), chunk, chunk + received);
            gotData = true;
            batchBytes += received;
            if (impl_->rxBuffer.size() >= 1024 * 1024 && onData_) {
                onData_();
                gotData = false;
            }
            if (batchBytes >= 4 * 1024 * 1024)
                break;
        } else {
            if (status == sf::Socket::Status::Disconnected ||
                status == sf::Socket::Status::Error)
                connectionLost = true;
            break;
        }
    }

    if (gotData && onData_)
        onData_();

    if (connectionLost) {
        // Remote closed / socket error (QTcpSocket::disconnected parity).
        impl_->socket->disconnect();
        state_ = State::Disconnected;
        if (onDisconnected_)
            onDisconnected_();
    }
}

std::size_t DeviceChannel::Read(char* buffer, std::size_t maxSize) {
    if (!impl_ || impl_->rxBuffer.empty())
        return 0;
    const std::size_t count = maxSize < impl_->rxBuffer.size() ? maxSize : impl_->rxBuffer.size();
    std::memcpy(buffer, impl_->rxBuffer.data(), count);
    impl_->rxBuffer.erase(0, count);
    return count;
}

std::size_t DeviceChannel::BytesAvailable() const {
    return impl_ ? impl_->rxBuffer.size() : 0;
}

bool DeviceChannel::Send(const char* data, std::size_t length) {
    if (state_ != State::Connected || !impl_)
        return false;
    // Full send, Partial-resend loop (QTcpSocket::write parity).
    std::size_t sent = 0;
    while (sent < length) {
        std::size_t sentNow = 0;
        const sf::Socket::Status status =
            impl_->socket->send(data + sent, length - sent, sentNow);
        if (status == sf::Socket::Status::Done) {
            sent += length - sent; // full-chunk variant: all bytes sent
        } else if (status == sf::Socket::Status::Partial) {
            sent += sentNow;
        } else {
            return false;
        }
    }
    return true;
}

void DeviceChannel::Abort() {
    if (!impl_)
        return;

    // A connect attempt still in flight is abandoned: the watcher thread's
    // blocking connect times out on its own (bounded by kConnectTimeoutMs)
    // and the thread is joined by the next ConnectAsync's Abort()... which
    // would block. Instead, signal abandonment and join here - the watcher
    // only touches the socket before posting connectResult, so a joined
    // watcher guarantees exclusive socket access.
    if (state_ == State::Connecting) {
        if (impl_->connectThread.joinable())
            impl_->connectThread.join(); // bounded by connect timeout
    }

    if (state_ != State::Disconnected && impl_->socket) {
        impl_->socket.reset();
        state_ = State::Disconnected;
    }
    impl_->rxBuffer.clear();
}
