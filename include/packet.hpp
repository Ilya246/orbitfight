#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <stdexcept>

namespace obf {

// Custom Packet class that replaces sf::Packet.
//
// Wire format (must match what the web client sends/receives):
//   Each packet on the wire is: [4-byte uint32 BE length][payload]
//   Payload values:
//     uint8_t/bool — 1 byte raw
//     uint16_t     — 2 bytes big-endian (htons/ntohs equivalent)
//     uint32_t     — 4 bytes big-endian (htonl/ntohl equivalent)
//     int32_t      — 4 bytes big-endian
//     double       — 8 bytes little-endian (native x86, raw reinterpret_cast)
//     string       — [4-byte uint32 BE length][UTF-8 bytes]
//
// This matches SFML's behavior: integers go through htons/htonl (network
// byte order = big-endian), while doubles use raw reinterpret_cast (native
// = little-endian on x86/x64). The length prefix also uses htonl (BE).

class Packet {
public:
    Packet() = default;

    // --- Write operators (mirror sf::Packet's operator<<) ---

    Packet& operator<<(uint8_t v) {
        append(&v, 1);
        return *this;
    }
    Packet& operator<<(int8_t v) {
        return *this << (uint8_t)v;
    }
    Packet& operator<<(uint16_t v) {
        uint8_t bytes[2] = {
            (uint8_t)((v >> 8) & 0xFF),
            (uint8_t)(v & 0xFF)
        };
        append(bytes, 2);
        return *this;
    }
    Packet& operator<<(int16_t v) {
        return *this << (uint16_t)v;
    }
    Packet& operator<<(uint32_t v) {
        uint8_t bytes[4] = {
            (uint8_t)((v >> 24) & 0xFF),
            (uint8_t)((v >> 16) & 0xFF),
            (uint8_t)((v >> 8) & 0xFF),
            (uint8_t)(v & 0xFF)
        };
        append(bytes, 4);
        return *this;
    }
    Packet& operator<<(int32_t v) {
        return *this << (uint32_t)v;
    }
    Packet& operator<<(double v) {
        // Native byte order (little-endian on x86) — matches SFML's raw
        // reinterpret_cast approach.
        uint8_t bytes[8];
        std::memcpy(bytes, &v, 8);
        append(bytes, 8);
        return *this;
    }
    Packet& operator<<(bool v) {
        return *this << (uint8_t)(v ? 1 : 0);
    }
    Packet& operator<<(const std::string& s) {
        *this << (uint32_t)s.size();
        append(s.data(), s.size());
        return *this;
    }
    Packet& operator<<(const char* s) {
        return *this << std::string(s);
    }

    // --- Read operators (mirror sf::Packet's operator>>) ---

    Packet& operator>>(uint8_t& v) {
        read(&v, 1);
        return *this;
    }
    Packet& operator>>(int8_t& v) {
        uint8_t u;
        read(&u, 1);
        v = (int8_t)u;
        return *this;
    }
    Packet& operator>>(uint16_t& v) {
        uint8_t bytes[2];
        read(bytes, 2);
        v = ((uint16_t)bytes[0] << 8) | (uint16_t)bytes[1];
        return *this;
    }
    Packet& operator>>(int16_t& v) {
        uint16_t u;
        *this >> u;
        v = (int16_t)u;
        return *this;
    }
    Packet& operator>>(uint32_t& v) {
        uint8_t bytes[4];
        read(bytes, 4);
        v = ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
            ((uint32_t)bytes[2] << 8) | (uint32_t)bytes[3];
        return *this;
    }
    Packet& operator>>(int32_t& v) {
        uint32_t u;
        *this >> u;
        v = (int32_t)u;
        return *this;
    }
    Packet& operator>>(double& v) {
        uint8_t bytes[8];
        read(bytes, 8);
        std::memcpy(&v, bytes, 8);
        return *this;
    }
    Packet& operator>>(bool& v) {
        uint8_t u;
        read(&u, 1);
        v = u != 0;
        return *this;
    }
    Packet& operator>>(std::string& s) {
        uint32_t len;
        *this >> len;
        s.resize(len);
        if (len > 0) read(&s[0], len);
        return *this;
    }

    // --- SFML-compatible accessors ---

    const void* getData() const { return data.data(); }
    std::size_t getDataSize() const { return data.size(); }
    bool endOfPacket() const { return readPos >= data.size(); }

    // Load raw payload bytes (without length prefix) into this packet for reading
    void loadPayload(const uint8_t* bytes, std::size_t len) {
        data.assign(bytes, bytes + len);
        readPos = 0;
    }

    // Clear for reuse
    void clear() {
        data.clear();
        readPos = 0;
    }

private:
    void append(const void* bytes, std::size_t len) {
        const auto* p = static_cast<const uint8_t*>(bytes);
        data.insert(data.end(), p, p + len);
    }

    void read(void* dest, std::size_t len) {
        if (readPos + len > data.size()) {
            throw std::runtime_error("Packet read out of bounds");
        }
        std::memcpy(dest, data.data() + readPos, len);
        readPos += len;
    }

    std::vector<uint8_t> data;
    std::size_t readPos = 0;
};

} // namespace obf
