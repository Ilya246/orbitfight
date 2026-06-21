// WebSocket server implementation for orbitfight.
//
// Implements RFC 6455 with optional TLS (wss://) support via OpenSSL.
// Without TLS, the server accepts plain ws:// connections. With TLS
// (when --tls-cert and --tls-key are provided), it accepts wss://
// connections — required when the web client is served over HTTPS
// (browsers block mixed-content ws:// from HTTPS pages).
//
// SHA-1 and Base64 are implemented inline to avoid other dependencies.
// OpenSSL is only needed for TLS mode.

#pragma once

#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <functional>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/select.h>

#include <openssl/ssl.h>
#include <openssl/err.h>

namespace obf {
namespace ws {

// --- Inline SHA-1 implementation ---

struct SHA1 {
    uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    uint8_t msg[64];
    uint64_t totalLen = 0;
    int msgLen = 0;

    static uint32_t rotl(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }

    void update(const uint8_t* data, size_t len) {
        totalLen += len;
        while (len > 0) {
            int copy = std::min((size_t)(64 - msgLen), len);
            std::memcpy(msg + msgLen, data, copy);
            msgLen += copy;
            data += copy;
            len -= copy;
            if (msgLen == 64) {
                processBlock();
                msgLen = 0;
            }
        }
    }

    void processBlock() {
        uint32_t w[80];
        for (int i = 0; i < 16; i++) {
            w[i] = ((uint32_t)msg[i*4] << 24) | ((uint32_t)msg[i*4+1] << 16) |
                   ((uint32_t)msg[i*4+2] << 8) | (uint32_t)msg[i*4+3];
        }
        for (int i = 16; i < 80; i++) {
            w[i] = rotl(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; i++) {
            uint32_t f, k;
            if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; }
            else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
            else { f = b ^ c ^ d; k = 0xCA62C1D6; }
            uint32_t temp = rotl(a, 5) + f + e + k + w[i];
            e = d; d = c; c = rotl(b, 30); b = a; a = temp;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }

    void final_(uint8_t out[20]) {
        msg[msgLen++] = 0x80;
        if (msgLen > 56) {
            while (msgLen < 64) msg[msgLen++] = 0;
            processBlock();
            msgLen = 0;
        }
        while (msgLen < 56) msg[msgLen++] = 0;
        uint64_t bitLen = totalLen * 8;
        for (int i = 7; i >= 0; i--) {
            msg[msgLen++] = (bitLen >> (i*8)) & 0xFF;
        }
        processBlock();
        for (int i = 0; i < 5; i++) {
            out[i*4] = (h[i] >> 24) & 0xFF;
            out[i*4+1] = (h[i] >> 16) & 0xFF;
            out[i*4+2] = (h[i] >> 8) & 0xFF;
            out[i*4+3] = h[i] & 0xFF;
        }
    }
};

// --- Inline Base64 implementation ---

static const char base64Chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

inline std::string base64Encode(const uint8_t* data, size_t len) {
    std::string result;
    result.reserve((len + 2) / 3 * 4);
    for (size_t i = 0; i < len; i += 3) {
        uint32_t n = (uint32_t)data[i] << 16;
        if (i + 1 < len) n |= (uint32_t)data[i+1] << 8;
        if (i + 2 < len) n |= data[i+2];
        result += base64Chars[(n >> 18) & 0x3F];
        result += base64Chars[(n >> 12) & 0x3F];
        result += (i + 1 < len) ? base64Chars[(n >> 6) & 0x3F] : '=';
        result += (i + 2 < len) ? base64Chars[n & 0x3F] : '=';
    }
    return result;
}

// Compute Sec-WebSocket-Accept from the client's key
inline std::string computeAccept(const std::string& key) {
    std::string combined = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    SHA1 sha;
    sha.update((const uint8_t*)combined.data(), combined.size());
    uint8_t hash[20];
    sha.final_(hash);
    return base64Encode(hash, 20);
}

// --- WebSocket connection state ---

struct Connection {
    int fd = -1;
    uint64_t id = 0;
    bool upgraded = false;
    std::string recvBuf;
    std::string ip;
    unsigned short port = 0;
    SSL* ssl = nullptr;         // non-null when TLS is used
    bool sslHandshakeDone = false;
};

// --- WebSocket server ---

class WebSocketServer {
public:
    using MessageCallback = std::function<void(uint64_t connId, const uint8_t* data, size_t len)>;
    using ConnectCallback = std::function<void(uint64_t connId, const std::string& ip, unsigned short port)>;
    using DisconnectCallback = std::function<void(uint64_t connId)>;

    WebSocketServer() = default;
    ~WebSocketServer() { stop(); }

    // Start server. If certPath and keyPath are non-empty, enables TLS (wss://).
    bool start(int port, const std::string& certPath = "", const std::string& keyPath = "") {
        // Initialize TLS if cert/key provided
        if (!certPath.empty() && !keyPath.empty()) {
            sslCtx = SSL_CTX_new(TLS_server_method());
            if (!sslCtx) {
                printf("Error: SSL_CTX_new failed\n");
                return false;
            }
            if (SSL_CTX_use_certificate_file(sslCtx, certPath.c_str(), SSL_FILETYPE_PEM) <= 0) {
                printf("Error: Failed to load certificate from %s\n", certPath.c_str());
                SSL_CTX_free(sslCtx);
                sslCtx = nullptr;
                return false;
            }
            if (SSL_CTX_use_PrivateKey_file(sslCtx, keyPath.c_str(), SSL_FILETYPE_PEM) <= 0) {
                printf("Error: Failed to load private key from %s\n", keyPath.c_str());
                SSL_CTX_free(sslCtx);
                sslCtx = nullptr;
                return false;
            }
            useTLS = true;
            printf("TLS enabled (cert: %s, key: %s)\n", certPath.c_str(), keyPath.c_str());
        }

        listenFd = socket(AF_INET, SOCK_STREAM, 0);
        if (listenFd < 0) { perror("socket"); return false; }

        int opt = 1;
        setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        int flags = fcntl(listenFd, F_GETFL, 0);
        fcntl(listenFd, F_SETFL, flags | O_NONBLOCK);

        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port);

        if (bind(listenFd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            perror("bind");
            close(listenFd);
            listenFd = -1;
            return false;
        }
        if (listen(listenFd, 64) < 0) {
            perror("listen");
            close(listenFd);
            listenFd = -1;
            return false;
        }
        printf("WebSocket server listening on port %d (%s)\n", port, useTLS ? "wss:// TLS" : "ws:// plain");
        return true;
    }

    void stop() {
        if (listenFd >= 0) {
            close(listenFd);
            listenFd = -1;
        }
        std::vector<int> fds;
        {
            std::lock_guard<std::mutex> lock(connsMutex);
            for (auto& [id, conn] : connections) {
                if (conn.ssl) { SSL_shutdown(conn.ssl); SSL_free(conn.ssl); }
                if (conn.fd >= 0) fds.push_back(conn.fd);
            }
            connections.clear();
        }
        for (int fd : fds) close(fd);
        if (sslCtx) { SSL_CTX_free(sslCtx); sslCtx = nullptr; }
    }

    void poll() {
        if (listenFd < 0) return;

        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(listenFd, &readSet);
        int maxFd = listenFd;

        std::vector<int> fds;
        {
            std::lock_guard<std::mutex> lock(connsMutex);
            for (auto& [id, conn] : connections) {
                if (conn.fd >= 0) {
                    FD_SET(conn.fd, &readSet);
                    if (conn.fd > maxFd) maxFd = conn.fd;
                    fds.push_back(conn.fd);
                }
            }
        }

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 0;

        int ret = select(maxFd + 1, &readSet, nullptr, nullptr, &tv);
        if (ret <= 0) return;

        // Accept new connections
        if (FD_ISSET(listenFd, &readSet)) {
            struct sockaddr_in clientAddr;
            socklen_t clientLen = sizeof(clientAddr);
            int clientFd = accept(listenFd, (struct sockaddr*)&clientAddr, &clientLen);
            if (clientFd >= 0) {
                int flags = fcntl(clientFd, F_GETFL, 0);
                fcntl(clientFd, F_SETFL, flags | O_NONBLOCK);

                uint64_t id = nextConnId++;
                {
                    std::lock_guard<std::mutex> lock(connsMutex);
                    connections[id] = Connection();
                    connections[id].fd = clientFd;
                    connections[id].id = id;
                    connections[id].ip = inet_ntoa(clientAddr.sin_addr);
                    connections[id].port = ntohs(clientAddr.sin_port);

                    // If TLS, create SSL object for this connection
                    if (useTLS && sslCtx) {
                        connections[id].ssl = SSL_new(sslCtx);
                        SSL_set_fd(connections[id].ssl, clientFd);
                        SSL_set_accept_state(connections[id].ssl);
                    }
                }
            }
        }

        // Read from existing connections
        std::vector<uint64_t> toRemove;
        for (int fd : fds) {
            if (!FD_ISSET(fd, &readSet)) continue;

            uint64_t id = findConnByFd(fd);
            if (!id) continue;

            Connection* conn;
            {
                std::lock_guard<std::mutex> lock(connsMutex);
                conn = &connections[id];
            }

            // If TLS and handshake not done, continue TLS handshake
            if (useTLS && conn->ssl && !conn->sslHandshakeDone) {
                int ret = SSL_accept(conn->ssl);
                if (ret <= 0) {
                    int err = SSL_get_error(conn->ssl, ret);
                    if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
                        continue; // need more data
                    }
                    // TLS handshake failed
                    toRemove.push_back(id);
                    continue;
                }
                conn->sslHandshakeDone = true;
            }

            // Read data (via TLS or plain)
            uint8_t buf[65536];
            ssize_t n;
            if (conn->ssl) {
                n = SSL_read(conn->ssl, buf, sizeof(buf));
                if (n <= 0) {
                    int err = SSL_get_error(conn->ssl, n);
                    if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) continue;
                    toRemove.push_back(id);
                    continue;
                }
            } else {
                n = recv(fd, buf, sizeof(buf), 0);
                if (n <= 0) {
                    toRemove.push_back(id);
                    continue;
                }
            }

            {
                std::lock_guard<std::mutex> lock(connsMutex);
                connections[id].recvBuf.append((char*)buf, n);
            }
            processConnection(id);
        }

        for (uint64_t id : toRemove) {
            removeConnection(id);
        }
    }

    void send(uint64_t connId, const uint8_t* data, size_t len) {
        std::lock_guard<std::mutex> lock(connsMutex);
        auto it = connections.find(connId);
        if (it == connections.end() || it->second.fd < 0) return;
        Connection& conn = it->second;

        // Build WebSocket binary frame
        std::vector<uint8_t> frame;
        frame.push_back(0x82); // FIN + binary
        if (len < 126) {
            frame.push_back((uint8_t)len);
        } else if (len < 65536) {
            frame.push_back(126);
            frame.push_back((len >> 8) & 0xFF);
            frame.push_back(len & 0xFF);
        } else {
            frame.push_back(127);
            for (int i = 7; i >= 0; i--) {
                frame.push_back((len >> (i*8)) & 0xFF);
            }
        }
        frame.insert(frame.end(), data, data + len);

        size_t sent = 0;
        while (sent < frame.size()) {
            ssize_t s;
            if (conn.ssl) {
                s = SSL_write(conn.ssl, frame.data() + sent, frame.size() - sent);
                if (s <= 0) {
                    int err = SSL_get_error(conn.ssl, s);
                    if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) continue;
                    break;
                }
            } else {
                s = ::send(conn.fd, frame.data() + sent, frame.size() - sent, 0);
                if (s <= 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
                    break;
                }
            }
            sent += s;
        }
    }

    void disconnect(uint64_t connId) {
        removeConnection(connId);
    }

    void setMessageCallback(MessageCallback cb) { messageCb = cb; }
    void setConnectCallback(ConnectCallback cb) { connectCb = cb; }
    void setDisconnectCallback(DisconnectCallback cb) { disconnectCb = cb; }

private:
    int listenFd = -1;
    std::unordered_map<uint64_t, Connection> connections;
    std::mutex connsMutex;
    std::atomic<uint64_t> nextConnId{1};

    bool useTLS = false;
    SSL_CTX* sslCtx = nullptr;

    MessageCallback messageCb;
    ConnectCallback connectCb;
    DisconnectCallback disconnectCb;

    uint64_t findConnByFd(int fd) {
        std::lock_guard<std::mutex> lock(connsMutex);
        for (auto& [id, conn] : connections) {
            if (conn.fd == fd) return id;
        }
        return 0;
    }

    void removeConnection(uint64_t id) {
        int fd = -1;
        SSL* ssl = nullptr;
        {
            std::lock_guard<std::mutex> lock(connsMutex);
            auto it = connections.find(id);
            if (it != connections.end()) {
                fd = it->second.fd;
                ssl = it->second.ssl;
                connections.erase(it);
            }
        }
        if (ssl) { SSL_shutdown(ssl); SSL_free(ssl); }
        if (fd >= 0) close(fd);
        if (disconnectCb) disconnectCb(id);
    }

    void processConnection(uint64_t id) {
        Connection* conn;
        {
            std::lock_guard<std::mutex> lock(connsMutex);
            conn = &connections[id];
        }
        if (!conn->upgraded) {
            size_t headerEnd = conn->recvBuf.find("\r\n\r\n");
            if (headerEnd == std::string::npos) return;

            std::string header = conn->recvBuf.substr(0, headerEnd);
            conn->recvBuf.erase(0, headerEnd + 4);

            std::string wsKey;
            size_t keyPos = header.find("Sec-WebSocket-Key:");
            if (keyPos == std::string::npos) keyPos = header.find("Sec-WebSocket-Key: ");
            if (keyPos == std::string::npos) {
                removeConnection(id);
                return;
            }
            size_t valStart = header.find(':', keyPos) + 1;
            size_t valEnd = header.find("\r\n", valStart);
            wsKey = header.substr(valStart, valEnd - valStart);
            while (!wsKey.empty() && (wsKey.front() == ' ' || wsKey.front() == '\t')) wsKey.erase(0, 1);
            while (!wsKey.empty() && (wsKey.back() == ' ' || wsKey.back() == '\t' || wsKey.back() == '\r')) wsKey.pop_back();

            std::string accept = computeAccept(wsKey);
            std::string response =
                "HTTP/1.1 101 Switching Protocols\r\n"
                "Upgrade: websocket\r\n"
                "Connection: Upgrade\r\n"
                "Sec-WebSocket-Accept: " + accept + "\r\n"
                "\r\n";

            if (conn->ssl) {
                SSL_write(conn->ssl, response.data(), response.size());
            } else {
                ::send(conn->fd, response.data(), response.size(), 0);
            }
            conn->upgraded = true;
            if (connectCb) connectCb(conn->id, conn->ip, conn->port);
        }

        while (!conn->recvBuf.empty()) {
            if (!parseFrame(id, conn)) break;
        }
    }

    bool parseFrame(uint64_t id, Connection* conn) {
        const std::string& buf = conn->recvBuf;
        if (buf.size() < 2) return false;

        size_t i = 0;
        uint8_t opcode = buf[i] & 0x0F;
        i++;
        bool masked = (buf[i] & 0x80) != 0;
        uint64_t payloadLen = buf[i] & 0x7F;
        i++;

        if (payloadLen == 126) {
            if (buf.size() < i + 2) return false;
            payloadLen = ((uint8_t)buf[i] << 8) | (uint8_t)buf[i+1];
            i += 2;
        } else if (payloadLen == 127) {
            if (buf.size() < i + 8) return false;
            payloadLen = 0;
            for (int j = 0; j < 8; j++) {
                payloadLen = (payloadLen << 8) | (uint8_t)buf[i + j];
            }
            i += 8;
        }

        uint8_t mask[4] = {0, 0, 0, 0};
        if (masked) {
            if (buf.size() < i + 4) return false;
            std::memcpy(mask, buf.data() + i, 4);
            i += 4;
        }

        if (buf.size() < i + payloadLen) return false;

        std::vector<uint8_t> payload(buf.begin() + i, buf.begin() + i + payloadLen);
        if (masked) {
            for (size_t j = 0; j < payload.size(); j++) {
                payload[j] ^= mask[j % 4];
            }
        }

        conn->recvBuf.erase(0, i + payloadLen);

        if (opcode == 0x8) {
            removeConnection(id);
            return false;
        } else if (opcode == 0x9) {
            sendPong(conn, payload.data(), payload.size());
            return true;
        } else if (opcode == 0xA) {
            return true;
        } else if (opcode == 0x1 || opcode == 0x2) {
            if (messageCb) messageCb(id, payload.data(), payload.size());
            return true;
        }
        return true;
    }

    void sendPong(Connection* conn, const uint8_t* data, size_t len) {
        std::vector<uint8_t> frame;
        frame.push_back(0x8A);
        if (len < 126) {
            frame.push_back((uint8_t)len);
        } else {
            frame.push_back(126);
            frame.push_back((len >> 8) & 0xFF);
            frame.push_back(len & 0xFF);
        }
        frame.insert(frame.end(), data, data + len);
        if (conn->ssl) {
            SSL_write(conn->ssl, frame.data(), frame.size());
        } else {
            ::send(conn->fd, frame.data(), frame.size(), 0);
        }
    }
};

} // namespace ws
} // namespace obf
