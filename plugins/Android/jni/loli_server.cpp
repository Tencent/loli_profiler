#include "loli_server.h"

#include <atomic>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <string>
#include <sstream>
#include <thread>
#include <vector>

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include <android/log.h>
#include <string.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>

#include "lz4/lz4.h"
#include "buffer.h"
#include "spinlock.h"
#include "loli_utils.h"

enum class loliCommands : std::uint8_t {
    SMAPS_DUMP = 0,
};

std::vector<io::buffer> cache_;
loli::spinlock cacheLock_;

char* buffer_ = NULL;
const std::size_t bandwidth_ = 3000;
std::atomic<bool> serverRunning_ {true};
std::atomic<bool> hasClient_ {false};
std::thread socketThread_;
bool started_ = false;
bool ignoreCache_ = false;

void loli_dump_smaps() {
    auto srcFile = fopen("/proc/self/smaps", "r");
    if (srcFile == nullptr) {
        LOLILOGE("Failed to fopen /proc/self/smaps error: %d", errno);
        return;
    }
    auto dstFile = fopen("/data/local/tmp/smaps.txt", "w");
    if (dstFile == nullptr) {
        fclose(srcFile);
        LOLILOGE("Failed to fopen /data/local/tmp/smaps.txt error: %d", errno);
        return;
    }
    int chunk = 1024;
    char *buf = (char*)malloc(chunk);
    if (buf == nullptr) {
        LOLILOGE("Failed to malloc(%i)", chunk);
        return;
    }
    size_t nread;
    while ((nread = fread(buf, 1, chunk, srcFile)) > 0) {
        int fileError = ferror(srcFile);
        if (fileError) {
            LOLILOGE("Error fread: %i", fileError);
            break;
        }
        fwrite(buf, 1, nread, dstFile);
        fileError = ferror(dstFile);
        if (fileError) {
            LOLILOGE("Error fwrite: %i", fileError);
            break;
        }
    }
    fclose(srcFile);
    fclose(dstFile);
    free(buf);
}

bool loli_server_started() {
    return started_;
}

void loli_server_loop(int sock) {
    std::vector<io::buffer> cacheCopy;
    std::vector<io::buffer> sendCache;
    io::buffer sendBuffer(10240);
    uint32_t compressBufferSize = 1024;
    char* compressBuffer = new char[compressBufferSize];
    struct timeval time;
    time.tv_sec = 0; // must initialize this value to prevent uninitialised memory
    time.tv_usec = 100;
    fd_set fds;
    int clientSock = -1;
    auto lastTickTime = std::chrono::steady_clock::now();
    while (serverRunning_) {
        if (!serverRunning_)
            break;
        if (!hasClient_) { // handle new connection
            FD_ZERO(&fds);
            FD_SET(sock, &fds);
            if (select(sock + 1, &fds, NULL, NULL, &time) < 1)
                continue;
            if (FD_ISSET(sock, &fds)) {
                clientSock = accept(sock, NULL, NULL);
                if (clientSock >= 0) {
                    LOLILOGI("Client connected");
                    hasClient_ = true;
                }
            }
        } else {
            // fill cached messages
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration<double, std::milli>(now - lastTickTime).count() > 66.6) {
                lastTickTime = now;
                std::lock_guard<loli::spinlock> lock(cacheLock_);
                if (sendCache.size() > 0) {
                    sendCache.insert(sendCache.end(), cache_.begin(), cache_.end());
                    cache_.clear();
                }
                else {
                    sendCache = std::move(cache_);
                }
            }
            // check for client connectivity
            FD_ZERO(&fds);
            FD_SET(clientSock, &fds);
            if (select(clientSock + 1, &fds, NULL, NULL, &time) > 0 && FD_ISSET(clientSock, &fds)) {
                int length = recv(clientSock, buffer_, BUFSIZ, 0);
                if (length <= 0) {
                    hasClient_ = false;
                    LOLILOGI("Client disconnected, ecode: %i", length);
                    continue;
                } else {
                    if (length > 0) {
                        std::uint8_t type = *reinterpret_cast<std::uint8_t*>(buffer_);
                        LOLILOGI("Server Recv: %i", (int)type);
                        if (type == static_cast<std::uint8_t>(loliCommands::SMAPS_DUMP)) {
                            LOLILOGI("Dumping smaps");
                            loli_dump_smaps();
                            io::buffer obuffer(8);
                            obuffer.clear();
                            obuffer << static_cast<uint8_t>(255) << static_cast<int32_t>(0);
                            loli_server_send(obuffer.data(), obuffer.size());
                            // loli_server_send("255\\0"); // loliFlags::COMMAND_ = 255, loliCommands::SMAPS_DUMP = 0
                            ignoreCache_ = true;
                        }
                    }
                }
            }
            // send cached messages with limited banwidth
            {
                auto cacheSize = sendCache.size();
                if (cacheSize <= bandwidth_) {
                    cacheCopy = std::move(sendCache);
                } else {
                    cacheCopy.reserve(bandwidth_);
                    for (std::size_t i = cacheSize - bandwidth_; i < cacheSize; i++)
                        cacheCopy.emplace_back(std::move(sendCache[i]));
                    sendCache.erase(sendCache.begin() + (cacheSize - bandwidth_), sendCache.end());
                }
            }
            if (cacheCopy.size() > 0) {
                sendBuffer.clear();
                for (auto& buffer : cacheCopy) {
                    sendBuffer << static_cast<uint16_t>(buffer.size());
                    sendBuffer.append(buffer);
                }
                // TODO: add option to turn off compression for performance reason
                std::uint32_t srcSize = static_cast<std::uint32_t>(sendBuffer.size());
                // lz4 compression
                uint32_t requiredSize = LZ4_compressBound(srcSize);
                if (requiredSize > compressBufferSize) { // enlarge compress buffer if necessary
                    compressBufferSize = static_cast<std::uint32_t>(requiredSize * 1.5f);
                    delete[] compressBuffer;
                    compressBuffer = new char[compressBufferSize];
                    // __android_log_print(ANDROID_LOG_INFO, "Loli", "Buffer exapnding: %i", static_cast<uint32_t>(compressBufferSize));
                }
                uint32_t compressSize = LZ4_compress_default(sendBuffer.data(), compressBuffer, srcSize, requiredSize);
                if (compressSize == 0) {
                    LOLILOGE("LZ4 compression failed!");
                } else {
                    compressSize += 4;
                    // send messages
                    send(clientSock, &compressSize, 4, 0); // send net buffer size
                    send(clientSock, &srcSize, 4, 0); // send uncompressed buffer size (for decompression)
                    send(clientSock, compressBuffer, compressSize - 4, 0); // then send data
                    // LOLILOGI("send size %i, compressed size %i, lineCount: %i", srcSize, compressSize, static_cast<int>(cacheCopy.size()));
                }
                cacheCopy.clear();
            }
        }
    }
    delete[] compressBuffer;
    close(sock);
    if (hasClient_)
        close(clientSock);
}

int loli_server_start(int port) {
    if (started_)
        return 0;
    // allocate buffer
    buffer_ = (char*)malloc(BUFSIZ);
    memset(buffer_, 0, BUFSIZ);
    // setup server addr
    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(port);
    // create socket
    int sock = socket(PF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        LOLILOGI("start.socket %i", sock);
        return -1;
    }
    // bind address
    int ecode = bind(sock, (struct sockaddr*)&serverAddr, sizeof(struct sockaddr));
    if (ecode < 0) {
        LOLILOGI("start.bind %i", ecode);
        return -1;
    }
    // set max send buffer
    int sendbuff = 327675;
    ecode = setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &sendbuff, sizeof(sendbuff));
    if (ecode < 0) {
        LOLILOGI("start.setsockopt %i", ecode);
        return -1;
    }
    // listen for incomming connections
    ecode = listen(sock, 2);
    if (ecode < 0) {
        LOLILOGI("start.listen %i", ecode);
        return -1;
    }
    started_ = true;
    serverRunning_ = true;
    hasClient_ = false;
    socketThread_ = std::thread(loli_server_loop, sock);
    return 0;
}

void loli_server_send(const char* data, unsigned int size) {
    if (ignoreCache_)
        return;
    std::lock_guard<loli::spinlock> lock(cacheLock_);
    cache_.emplace_back(io::buffer(data, size));
}

void loli_server_shutdown() {
    if (!started_)
        return;
    serverRunning_ = false;
    hasClient_ = false;
    socketThread_.join();
    free(buffer_);
    buffer_ = NULL;
    started_ = false;
}

#ifdef __cplusplus
}
#endif // __cplusplus