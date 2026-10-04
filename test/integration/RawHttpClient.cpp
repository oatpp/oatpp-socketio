#include "RawHttpClient.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <cctype>
#include <string>

namespace siotest {

namespace {

void setSocketTimeouts(int fd, int timeoutMs) {
  timeval tv;
  tv.tv_sec = timeoutMs / 1000;
  tv.tv_usec = (timeoutMs % 1000) * 1000;
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

int connectTo(unsigned short port, int timeoutMs) {
  int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    return -1;
  }
  setSocketTimeouts(fd, timeoutMs);

  sockaddr_in addr;
  std::memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    ::close(fd);
    return -1;
  }
  return fd;
}

bool sendAll(int fd, const std::string& data) {
  size_t sent = 0;
  while (sent < data.size()) {
    const ssize_t n = ::send(fd, data.data() + sent, data.size() - sent, 0);
    if (n <= 0) {
      return false;
    }
    sent += static_cast<size_t>(n);
  }
  return true;
}

std::string toLower(const std::string& in) {
  std::string out = in;
  for (char& c : out) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return out;
}

/** value of a header, "" if absent */
std::string headerValue(const std::string& headers, const std::string& name) {
  const std::string lower = toLower(headers);
  const std::string needle = toLower(name) + ":";
  size_t pos = lower.find(needle);
  while (pos != std::string::npos) {
    if (pos == 0 || headers[pos - 1] == '\n') {
      size_t value = pos + needle.size();
      while (value < headers.size() && (headers[value] == ' ' || headers[value] == '\t')) {
        value++;
      }
      size_t end = headers.find("\r\n", value);
      if (end == std::string::npos) {
        end = headers.size();
      }
      return headers.substr(value, end - value);
    }
    pos = lower.find(needle, pos + 1);
  }
  return std::string();
}

/** send the request, read the whole response, close the socket */
RawResponse sendAndRead(int fd, const std::string& request,
                        bool halfCloseAfterSend = false) {
  RawResponse response;

  if (!sendAll(fd, request)) {
    ::close(fd);
    response.peerClosed = true;
    return response;
  }

  if (halfCloseAfterSend) {
    // "that is all the body" - while still listening for the answer
    ::shutdown(fd, SHUT_WR);
  }

  std::string raw;
  size_t headerEnd = std::string::npos;
  size_t contentLength = 0;
  bool haveHeaders = false;

  while (true) {
    char buffer[4096];
    const ssize_t n = ::recv(fd, buffer, sizeof(buffer), 0);
    if (n > 0) {
      raw.append(buffer, static_cast<size_t>(n));

      if (!haveHeaders) {
        headerEnd = raw.find("\r\n\r\n");
        if (headerEnd != std::string::npos) {
          haveHeaders = true;
          response.headers = raw.substr(0, headerEnd);
          const std::string length = headerValue(response.headers, "Content-Length");
          if (!length.empty()) {
            contentLength = static_cast<size_t>(std::strtoul(length.c_str(), nullptr, 10));
          } else {
            contentLength = 0;  // read until close
          }
        }
      }

      if (haveHeaders) {
        const size_t bodySoFar = raw.size() - headerEnd - 4;
        if (contentLength && bodySoFar >= contentLength) {
          break;
        }
      }
      continue;
    }
    if (n == 0) {
      break;  // server closed
    }
    // n < 0
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      response.timedOut = true;
    }
    break;
  }

  ::close(fd);

  if (raw.empty()) {
    // nothing at all: either we timed out, or the peer closed on us
    response.peerClosed = !response.timedOut;
    return response;  // ok = false
  }

  if (!haveHeaders) {
    response.peerClosed = !response.timedOut;
    return response;  // malformed
  }

  // "HTTP/1.1 200 OK"
  const std::string statusLine = raw.substr(0, raw.find("\r\n"));
  const size_t firstSpace = statusLine.find(' ');
  if (firstSpace == std::string::npos) {
    return response;
  }
  response.status = std::atoi(statusLine.c_str() + firstSpace + 1);
  response.body = raw.substr(headerEnd + 4);
  if (contentLength && response.body.size() > contentLength) {
    response.body.resize(contentLength);
  }
  response.ok = true;
  return response;
}

}  // namespace

RawResponse httpRequest(unsigned short port, const std::string& method,
                        const std::string& path, const std::string& body,
                        int timeoutMs) {

  const int fd = connectTo(port, timeoutMs);
  if (fd < 0) {
    RawResponse response;
    response.connectFailed = true;
    return response;  // ok = false
  }

  std::string request;
  request += method;
  request += " ";
  request += path;
  request += " HTTP/1.1\r\n";
  request += "Host: 127.0.0.1:" + std::to_string(port) + "\r\n";
  request += "Connection: close\r\n";
  request += "Accept: */*\r\n";
  if (!body.empty() || method == "POST" || method == "PUT") {
    // Content-Length must be sent even for an empty body, otherwise the
    // server waits for a body that never arrives
    request += "Content-Type: text/plain;charset=UTF-8\r\n";
    request += "Content-Length: " + std::to_string(body.size()) + "\r\n";
  }
  request += "\r\n";
  request += body;

  return sendAndRead(fd, request);
}

RawResponse httpRequestBodyToEof(unsigned short port, const std::string& path,
                                 const std::string& body, int timeoutMs) {

  const int fd = connectTo(port, timeoutMs);
  if (fd < 0) {
    RawResponse response;
    response.connectFailed = true;
    return response;
  }

  // No Content-Length and no Transfer-Encoding: the body is delimited by the
  // end of the stream, which is what the half-close below announces.
  std::string request;
  request += "POST ";
  request += path;
  request += " HTTP/1.1\r\n";
  request += "Host: 127.0.0.1:" + std::to_string(port) + "\r\n";
  request += "Connection: close\r\n";
  request += "Accept: */*\r\n";
  request += "Content-Type: text/plain;charset=UTF-8\r\n";
  request += "\r\n";
  request += body;

  return sendAndRead(fd, request, /*halfCloseAfterSend = */ true);
}

RawResponse httpRequestHeadersOnly(unsigned short port, const std::string& path,
                                   size_t declaredLength, int timeoutMs) {

  const int fd = connectTo(port, timeoutMs);
  if (fd < 0) {
    RawResponse response;
    response.connectFailed = true;
    return response;
  }

  std::string request;
  request += "POST ";
  request += path;
  request += " HTTP/1.1\r\n";
  request += "Host: 127.0.0.1:" + std::to_string(port) + "\r\n";
  request += "Connection: close\r\n";
  request += "Accept: */*\r\n";
  request += "Content-Type: text/plain;charset=UTF-8\r\n";
  request += "Content-Length: " + std::to_string(declaredLength) + "\r\n";
  request += "\r\n";
  // ... and no body, ever

  return sendAndRead(fd, request);
}

bool waitForPort(unsigned short port, int timeoutMs) {
  const long stepMs = 50;
  for (long waited = 0; waited < timeoutMs; waited += stepMs) {
    const int fd = connectTo(port, 500);
    if (fd >= 0) {
      ::close(fd);
      return true;
    }
    ::usleep(static_cast<useconds_t>(stepMs * 1000));
  }
  return false;
}

}  // namespace siotest
