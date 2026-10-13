#include "RawWsClient.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <chrono>
#include <random>
#include <thread>

namespace siotest {

namespace {

void setSocketTimeouts(int fd, int timeoutMs) {
  timeval tv;
  tv.tv_sec = timeoutMs / 1000;
  tv.tv_usec = (timeoutMs % 1000) * 1000;
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

bool sendAll(int fd, const void* data, size_t size) {
  const uint8_t* bytes = static_cast<const uint8_t*>(data);
  size_t sent = 0;
  while (sent < size) {
    const ssize_t n = ::send(fd, bytes + sent, size - sent, 0);
    if (n <= 0) {
      return false;
    }
    sent += static_cast<size_t>(n);
  }
  return true;
}

/** why a read did not deliver the requested bytes */
enum class ReadStatus { Ok, Timeout, EndOfStream, Error };

/**
 * Read exactly @p size bytes, distinguishing "the peer is just slow" from
 * "the peer went away". Without this a close is indistinguishable from a
 * timeout and a test cannot assert that the server dropped the connection.
 */
ReadStatus readAllStatus(int fd, uint8_t* out, size_t size) {
  size_t got = 0;
  while (got < size) {
    const ssize_t n = ::recv(fd, out + got, size - got, 0);
    if (n > 0) {
      got += static_cast<size_t>(n);
      continue;
    }
    if (n == 0) {
      return ReadStatus::EndOfStream;  // peer closed
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      return ReadStatus::Timeout;
    }
    return ReadStatus::Error;
  }
  return ReadStatus::Ok;
}

}  // namespace

RawWsClient::~RawWsClient() {
  close();
}

void RawWsClient::close() {
  if (m_fd >= 0) {
    ::close(m_fd);
    m_fd = -1;
  }
}

bool RawWsClient::connect(unsigned short port, const std::string& path,
                          int timeoutMs) {

  m_closed = false;
  m_closeFrame = false;

  m_fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (m_fd < 0) {
    return false;
  }
  setSocketTimeouts(m_fd, timeoutMs);

  sockaddr_in addr;
  std::memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  if (::connect(m_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    close();
    return false;
  }

  // random Sec-WebSocket-Key (base64 of 16 bytes)
  uint8_t keyBytes[16];
  {
    std::random_device rd;
    for (size_t i = 0; i < sizeof(keyBytes); i++) {
      keyBytes[i] = static_cast<uint8_t>(rd() & 0xFF);
    }
  }
  static const char* b64 =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string key;
  for (size_t i = 0; i < sizeof(keyBytes); i += 3) {
    const uint32_t chunk = (keyBytes[i] << 16) |
                           ((i + 1 < sizeof(keyBytes)) ? keyBytes[i + 1] << 8 : 0) |
                           ((i + 2 < sizeof(keyBytes)) ? keyBytes[i + 2] : 0);
    key.push_back(b64[(chunk >> 18) & 0x3F]);
    key.push_back(b64[(chunk >> 12) & 0x3F]);
    key.push_back(i + 1 < sizeof(keyBytes) ? b64[(chunk >> 6) & 0x3F] : '=');
    key.push_back(i + 2 < sizeof(keyBytes) ? b64[chunk & 0x3F] : '=');
  }

  std::string request = "GET " + path + " HTTP/1.1\r\n" +
                        "Host: 127.0.0.1:" + std::to_string(port) + "\r\n" +
                        "Upgrade: websocket\r\n" +
                        "Connection: Upgrade\r\n" +
                        "Sec-WebSocket-Key: " + key + "\r\n" +
                        "Sec-WebSocket-Version: 13\r\n" +
                        "\r\n";

  if (!sendAll(m_fd, request.data(), request.size())) {
    close();
    return false;
  }

  // read the response headers
  std::string raw;
  char byte = 0;
  while (raw.find("\r\n\r\n") == std::string::npos) {
    const ssize_t n = ::recv(m_fd, &byte, 1, 0);
    if (n <= 0) {
      close();
      return false;
    }
    raw.push_back(byte);
    if (raw.size() > 65536) {
      close();
      return false;
    }
  }

  // 101 Switching Protocols, and an accept header (we do not verify the
  // SHA-1 of the key here, only that the handshake completed)
  const bool switched = raw.compare(0, 12, "HTTP/1.1 101") == 0;
  const bool accepted =
      raw.find("Sec-WebSocket-Accept:") != std::string::npos ||
      raw.find("sec-websocket-accept:") != std::string::npos;

  if (!switched || !accepted) {
    close();
    return false;
  }
  return true;
}

bool RawWsClient::send(const std::string& payload) {
  if (m_fd < 0) {
    return false;
  }

  uint8_t header[10];
  size_t headerSize = 2;
  header[0] = 0x81;  // FIN + text

  const size_t length = payload.size();
  if (length < 126) {
    header[1] = static_cast<uint8_t>(0x80 | length);
  } else if (length <= 0xFFFF) {
    header[1] = 0x80 | 126;
    header[2] = static_cast<uint8_t>((length >> 8) & 0xFF);
    header[3] = static_cast<uint8_t>(length & 0xFF);
    headerSize = 4;
  } else {
    header[1] = 0x80 | 127;
    for (int i = 0; i < 8; i++) {
      header[2 + i] = static_cast<uint8_t>(
          (length >> (56 - 8 * i)) & 0xFF);
    }
    headerSize = 10;
  }

  uint8_t mask[4];
  {
    std::random_device rd;
    for (size_t i = 0; i < 4; i++) {
      mask[i] = static_cast<uint8_t>(rd() & 0xFF);
    }
  }

  std::string masked(payload.size(), '\0');
  for (size_t i = 0; i < payload.size(); i++) {
    masked[i] = payload[i] ^ mask[i % 4];
  }

  if (!sendAll(m_fd, header, headerSize)) {
    return false;
  }
  if (!sendAll(m_fd, mask, 4)) {
    return false;
  }
  return sendAll(m_fd, masked.data(), masked.size());
}

bool RawWsClient::receive(std::string& payload, unsigned int* opcode,
                          int timeoutMs) {
  payload.clear();
  if (m_fd < 0) {
    m_closed = true;
    return false;
  }
  setSocketTimeouts(m_fd, timeoutMs);

  // a read that fails for any reason other than a timeout means the
  // connection is gone; remember it so closed() can be asserted on later
  const auto failed = [&](ReadStatus st) -> bool {
    if (st != ReadStatus::Timeout) {
      m_closed = true;
    }
    return false;
  };

  uint8_t header[2];
  ReadStatus st = readAllStatus(m_fd, header, 2);
  if (st != ReadStatus::Ok) {
    return failed(st);
  }

  if (opcode) {
    *opcode = header[1] & 0x0F;
  }

  uint64_t length = header[1] & 0x7F;
  if (length == 126) {
    uint8_t ext[2];
    st = readAllStatus(m_fd, ext, 2);
    if (st != ReadStatus::Ok) {
      return failed(st);
    }
    length = (static_cast<uint64_t>(ext[0]) << 8) | ext[1];
  } else if (length == 127) {
    uint8_t ext[8];
    st = readAllStatus(m_fd, ext, 8);
    if (st != ReadStatus::Ok) {
      return failed(st);
    }
    length = 0;
    for (int i = 0; i < 8; i++) {
      length = (length << 8) | ext[i];
    }
  }

  if (length > (16u * 1024 * 1024)) {
    return false;  // refuse to allocate nonsense
  }

  // server frames must not be masked
  const bool masked = (header[1] & 0x80) != 0;
  uint8_t mask[4] = {0, 0, 0, 0};
  if (masked) {
    st = readAllStatus(m_fd, mask, 4);
    if (st != ReadStatus::Ok) {
      return failed(st);
    }
  }

  payload.resize(static_cast<size_t>(length));
  if (length) {
    st = readAllStatus(m_fd, reinterpret_cast<uint8_t*>(&payload[0]), length);
    if (st != ReadStatus::Ok) {
      return failed(st);
    }
  }
  if (masked) {
    for (size_t i = 0; i < payload.size(); i++) {
      payload[i] = static_cast<char>(payload[i] ^ mask[i % 4]);
    }
  }

  if ((header[1] & 0x0F) == 0x8) {  // close
    m_closed = true;
    m_closeFrame = true;
  }
  return true;
}

bool RawWsClient::receiveUntil(const std::string& prefix, std::string& payload,
                               int timeoutMs) {
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);

  while (std::chrono::steady_clock::now() < deadline) {
    std::string frame;
    unsigned int opcode = 0;
    if (!receive(frame, &opcode, 1000)) {
      if (m_closed) {
        return false;  // gone, do not keep polling a dead socket
      }
      continue;  // timeout, retry until the deadline
    }
    if (opcode == 0x9) {  // ping -> answer with a pong
      send(frame);
      continue;
    }
    if (opcode == 0x8) {  // close
      return false;
    }
    if (frame.compare(0, prefix.size(), prefix) == 0) {
      payload = frame;
      return true;
    }
  }
  return false;
}

void RawWsClient::sendClose() {
  if (m_fd < 0) {
    return;
  }
  // FIN + close (0x88), masked, empty payload
  uint8_t frame[6];
  frame[0] = 0x88;
  frame[1] = 0x80;
  {
    std::random_device rd;
    for (int i = 0; i < 4; i++) {
      frame[2 + i] = static_cast<uint8_t>(rd() & 0xFF);
    }
  }
  sendAll(m_fd, frame, sizeof(frame));
  m_closed = true;
  close();
}

bool RawWsClient::waitForClose(int timeoutMs) {
  if (m_closed) {
    return true;
  }
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);

  while (std::chrono::steady_clock::now() < deadline) {
    std::string frame;
    unsigned int opcode = 0;
    if (!receive(frame, &opcode, 500)) {
      if (m_closed) {
        return true;
      }
    } else if (opcode == 0x9) {  // keep answering pings while we wait
      send(frame);
    } else if (opcode == 0x8) {
      return true;
    }
  }
  return m_closed;
}

}  // namespace siotest
