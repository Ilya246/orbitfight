#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace obf {

// Start the WebSocket server on the given port.
// When certPath and keyPath are non-empty, TLS (wss://) is enabled.
// Returns true on success; on failure prints an error and returns false.
bool wsStart(int port, const std::string& certPath = {}, const std::string& keyPath = {});

void wsStop();

// Drain the accumulated event queue.
void wsPoll();

// Send raw bytes to a connected client by connection id.
// No-op if the connection is not found.
void wsSend(uint64_t connId, const uint8_t* data, size_t len);

// Close a client connection by connection id.
// No-op if the connection is not found.
void wsDisconnect(uint64_t connId);

} // namespace obf
