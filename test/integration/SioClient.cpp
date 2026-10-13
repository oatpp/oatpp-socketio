#include "SioClient.hpp"

#include <chrono>

namespace siotest {

namespace {

/** extract "sid":"..." from an engine.io/socket.io open packet */
std::string extractSid(const std::string& packet) {
  const std::string key = "\"sid\":\"";
  const size_t start = packet.find(key);
  if (start == std::string::npos) {
    return std::string();
  }
  const size_t from = start + key.size();
  const size_t end = packet.find('"', from);
  if (end == std::string::npos) {
    return std::string();
  }
  return packet.substr(from, end - from);
}

std::string pollingQuery(const std::string& sid) {
  std::string path = "/socket.io/?EIO=4&transport=polling";
  if (!sid.empty()) {
    path += "&sid=" + sid;
  }
  return path;
}

}  // namespace

bool PollClient::open() {
  m_closed = false;
  const RawResponse response =
      httpRequest(m_port, "GET", pollingQuery(std::string()));
  if (!response.ok || response.status != 200) {
    return false;
  }
  m_openPacket = response.body;
  m_sid = extractSid(response.body);
  return !m_sid.empty();
}

bool PollClient::post(const std::string& sioPacket) {
  // on the wire every socket.io packet is wrapped in an engine.io MESSAGE
  // ("4"), both up- and downstream
  const RawResponse response =
      httpRequest(m_port, "POST", pollingQuery(m_sid), "4" + sioPacket);
  return response.ok && response.status == 200;
}

bool PollClient::sioConnect(const std::string& nsp) {
  std::string packet = "0";  // socket.io CONNECT
  if (nsp != "/") {
    packet = "0" + nsp + ",";
  }
  if (!post(packet)) {
    return false;
  }
  const std::string answer = poll();
  if (!startsWith(answer, "40")) {
    return false;
  }
  m_sioSid = extractSid(answer);
  return !m_sioSid.empty();
}

std::string PollClient::poll(int timeoutMs) {
  const RawResponse response =
      httpRequest(m_port, "GET", pollingQuery(m_sid), std::string(), timeoutMs);
  if (!response.ok) {
    if (response.connectFailed || response.peerClosed) {
      m_closed = true;
    }
    return std::string();
  }
  return response.body;
}

PollClient::ConnState PollClient::state(std::string* delivered, int timeoutMs) {
  if (m_sid.empty()) {
    m_closed = true;
    return ConnState::Closed;
  }

  const RawResponse response =
      httpRequest(m_port, "GET", pollingQuery(m_sid), std::string(), timeoutMs);

  // nothing listening, or the peer hung up on the pending request
  if (response.connectFailed || response.peerClosed) {
    m_closed = true;
    return ConnState::Closed;
  }

  // the server is holding the long-poll open: alive, nothing queued
  if (response.timedOut) {
    return ConnState::Alive;
  }

  // e.g. HTTP 400 "sid not found": the server has forgotten this connection
  if (!response.ok || response.status != 200) {
    m_closed = true;
    return ConnState::Closed;
  }

  if (delivered) {
    *delivered = response.body;
  }

  if (response.body == "1") {  // engine.io CLOSE
    m_closed = true;
    return ConnState::Closed;
  }

  return ConnState::Alive;
}

bool PollClient::waitForClose(int timeoutMs) {
  if (m_closed) {
    return true;
  }
  if (m_sid.empty()) {
    m_closed = true;
    return true;
  }

  // One single long-poll held open for the whole timeout - *not* a loop of
  // short polls. The server gives each connection exactly one outstanding
  // poll and closes the connection when a second one arrives, so a client
  // that polls again while the first is still held would tear down the very
  // connection it is trying to observe. Holding one open is also the natural
  // way to see a close: the server answers it when it has something, or hangs
  // up on it.
  const RawResponse response =
      httpRequest(m_port, "GET", pollingQuery(m_sid), std::string(), timeoutMs);

  if (response.connectFailed || response.peerClosed) {
    m_closed = true;
    return true;
  }
  if (!response.ok || response.status != 200) {
    m_closed = true;  // e.g. 400 "closed" / "no conn"
    return true;
  }
  if (response.body == "1") {  // engine.io CLOSE
    m_closed = true;
    return true;
  }
  return false;  // still alive when the timeout expired
}

bool PollClient::close(int timeoutMs) {
  if (m_sid.empty()) {
    return false;
  }
  // engine.io CLOSE is a bare "1"; unlike a socket.io packet it is not wrapped
  // in an engine.io MESSAGE ("4")
  const RawResponse response =
      httpRequest(m_port, "POST", pollingQuery(m_sid), "1", timeoutMs);
  m_closed = true;
  return response.ok && response.status == 200;
}

PollClient::~PollClient() {
  if (!m_autoClose || m_closed || m_sid.empty()) {
    return;
  }
  // Destructors run cleanup, they do not report: a server that already went
  // away is the normal case here, and throwing from here would take the whole
  // test binary down and hide the real failure.
  try {
    close(1000);
  } catch (...) {
  }
}

}  // namespace siotest
