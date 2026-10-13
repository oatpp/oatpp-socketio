#include "oatpp_sio/sio/wire.hpp"

namespace oatpp_sio {
namespace sio {

namespace {

inline bool isDigit(char c) {
  return c >= '0' && c <= '9';
}

/** characters that cannot be part of a namespace path */
inline bool endsNamespace(char c) {
  return c == ',' || c == '[' || c == '{';
}

/** minimal escaping for putting a string into a JSON string literal */
std::string jsonEscape(const std::string& in) {
  std::string out;
  out.reserve(in.size() + 8);
  for (char c : in) {
    switch (c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        out.push_back(c);
    }
  }
  return out;
}

}  // namespace

bool parsePacket(const std::string& data, WirePacket& out) {

  out = WirePacket{};

  const size_t n = data.size();
  size_t i = 0;

  // -- optional number of binary attachments: "<digits>-" --------------------
  if (i < n && isDigit(data[i])) {
    const size_t start = i;
    while (i < n && isDigit(data[i])) {
      i++;
    }
    if (i < n && data[i] == '-') {
      out.attachments = data.substr(start, i - start);
      i++;  // jump over '-'
    } else {
      // not a binary prefix - rewind, it is an ack id or the payload
      i = start;
    }
  }

  // -- optional namespace: "/name", terminated by ',' or by the payload ------
  if (i < n && data[i] == '/') {
    const size_t start = i;
    while (i < n && !endsNamespace(data[i])) {
      i++;
    }
    out.nsp = data.substr(start, i - start);
    if (i < n && data[i] == ',') {
      i++;  // jump over ','
    }
  }

  // -- optional ack id -------------------------------------------------------
  if (i < n && isDigit(data[i])) {
    const size_t start = i;
    while (i < n && isDigit(data[i])) {
      i++;
    }
    out.ack = data.substr(start, i - start);
  }

  // -- the rest is the payload -----------------------------------------------
  if (i < n) {
    out.payload = data.substr(i);
  }

  if (!out.payload.empty()) {
    const char first = out.payload[0];
    if (first != '[' && first != '{') {
      return false;  // payload must be a JSON array or object
    }
  }

  return true;
}

std::string encodePacket(PacketType type, const WirePacket& packet) {

  std::string out;
  out.reserve(8 + packet.nsp.size() + packet.ack.size() + packet.payload.size());

  out.push_back(static_cast<char>(type));

  if (!packet.attachments.empty()) {
    out += packet.attachments;
    out.push_back('-');
  }
  if (!packet.nsp.empty() && packet.nsp != "/") {
    out += packet.nsp;
    out.push_back(',');
  }
  out += packet.ack;
  out += packet.payload;

  return out;
}

std::string encodeConnectAck(const std::string& nsp, const std::string& sid) {
  WirePacket packet;
  packet.nsp = nsp;
  packet.payload = "{\"sid\":\"" + jsonEscape(sid) + "\"}";
  return encodePacket(PacketType::connect, packet);
}

std::string encodeConnectError(const std::string& nsp,
                               const std::string& message) {
  WirePacket packet;
  packet.nsp = nsp;
  packet.payload = "{\"message\":\"" + jsonEscape(message) + "\"}";
  return encodePacket(PacketType::connectError, packet);
}

std::string encodeDisconnect(const std::string& nsp) {
  WirePacket packet;
  packet.nsp = nsp;
  return encodePacket(PacketType::disconnect, packet);
}

std::string encodeEvent(const std::string& nsp, const std::string& payload) {
  WirePacket packet;
  packet.nsp = nsp;
  packet.payload = payload;
  return encodePacket(PacketType::event, packet);
}

std::string encodeAck(const std::string& nsp, const std::string& ackId,
                      const std::string& payload) {
  WirePacket packet;
  packet.nsp = nsp;
  packet.ack = ackId;
  packet.payload = payload;
  return encodePacket(PacketType::ack, packet);
}

}  // namespace sio
}  // namespace oatpp_sio
