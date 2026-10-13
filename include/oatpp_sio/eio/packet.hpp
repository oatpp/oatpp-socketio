/***************************************************************************
 *
 * Engine.IO v4 packet framing.
 *
 * An engine.io packet on the wire is a single type character followed by the
 * (binary safe) payload:
 *
 *   0{"sid":"abc",...}      open
 *   2                      ping
 *   3                      pong
 *   4<socket.io packet>    message
 *   5                      upgrade
 *   6                      noop
 *   b<binary>              binary message
 *
 * See https://socket.io/docs/v4/engine-io-protocol/
 *
 ***************************************************************************/

#ifndef oatpp_sio_eio_packet_hpp
#define oatpp_sio_eio_packet_hpp

#include <cstddef>
#include <cstring>
#include <string>

namespace oatpp_sio {
namespace eio {

/** engine.io packet types (wire single-char identifiers) */
typedef enum {
  eioOpen = '0',
  eioClose,
  eioPing,
  eioPong,
  eioMessage,
  eiouUgrade,
  eioNoop,
  eioBinary = 'b'
} EioPacketType;

/**
 * Encode one engine.io packet: `"<type><payload>"`.
 * @param pkt packet type.
 * @param msg payload, may contain '\0'.
 * @return the wire packet.
 */
inline std::string pktEncode(EioPacketType pkt, const std::string& msg) {
  std::string result;
  result.resize(msg.size() + 1);
  result[0] = static_cast<char>(pkt);
  if (!msg.empty()) {
    std::memcpy(&result[1], msg.data(), msg.size());
  }
  return result;
}

/**
 * Type byte of a wire packet.
 * @return the type character, or `'\0'` if @p raw is empty.
 */
inline char pktType(const std::string& raw) {
  return raw.empty() ? '\0' : raw[0];
}

/**
 * Payload of a wire packet (everything after the type byte).
 * @return the payload, `""` for an empty packet. Never reads out of bounds.
 */
inline std::string pktPayload(const std::string& raw) {
  return raw.size() > 1 ? raw.substr(1) : std::string();
}

}  // namespace eio
}  // namespace oatpp_sio

#endif /* oatpp_sio_eio_packet_hpp */
