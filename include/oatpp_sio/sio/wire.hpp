/***************************************************************************
 *
 * socket.io v4 packet framing (the payload of an engine.io MESSAGE packet).
 *
 * Wire format, see https://github.com/socketio/socket.io/tree/main/packages/socket.io-parser:
 *
 *   <type>[<attachments>"-"][<nsp>","][<ack>][<payload>]
 *
 *   0{"sid":"abc"}          connect, root namespace
 *   0/chat,{"sid":"abc"}   connect, /chat namespace
 *   1                        disconnect
 *   2["event",...]           event
 *   2/chat,12["event",...]   event on /chat with ack id 12
 *   312["reply"]             ack for id 12
 *   4{"message":"..."}       connect_error
 *
 * The type character is stripped before parsePacket() is called and added
 * back by the encoders, so the functions here deal with the remainder.
 *
 ***************************************************************************/

#ifndef oatpp_sio_sio_wire_hpp
#define oatpp_sio_sio_wire_hpp

#include <string>

namespace oatpp_sio {
namespace sio {

/** socket.io packet types (wire single-char identifiers) */
enum class PacketType : char {
  connect = '0',
  disconnect = '1',
  event = '2',
  ack = '3',
  connectError = '4',
  binaryEvent = '5',
  binaryAck = '6'
};

/** a decoded socket.io packet (without the type character) */
struct WirePacket {
  /** number of binary attachments as digits, empty for non-binary packets */
  std::string attachments;
  /** namespace, "/" for the default one */
  std::string nsp = "/";
  /** ack id, empty if the packet carries none */
  std::string ack;
  /** JSON payload, may be empty */
  std::string payload;
};

/**
 * Parse the remainder of a socket.io packet (everything after the type
 * character).
 *
 * The parser is bounds safe: any input, including truncated or empty ones,
 * is handled without reading out of range. It is deliberately lenient about
 * the separator comma (the reference client always emits one after a named
 * namespace, `0/chat,`, but `0/chat` is accepted too).
 *
 * @param data packet content without the leading type character.
 * @param out filled in on return.
 * @return false if the packet is malformed (a payload must start with '[' or
 *         '{'; a binary prefix must be all digits followed by '-').
 */
bool parsePacket(const std::string& data, WirePacket& out);

/**
 * Encode a packet: `"<type>[attachments-][nsp,][ack][payload]"`.
 * @param type packet type.
 * @param packet content.
 * @return the wire representation (without the engine.io framing).
 */
std::string encodePacket(PacketType type, const WirePacket& packet);

/** `0[<nsp>,]{"sid":"<sid>"}` */
std::string encodeConnectAck(const std::string& nsp, const std::string& sid);

/** `4[<nsp>,]{"message":"<message>"}` */
std::string encodeConnectError(const std::string& nsp,
                               const std::string& message);

/** `1[<nsp>,]` (the reference client emits the comma, see encodeDisconnect) */
std::string encodeDisconnect(const std::string& nsp);

/** `2[<nsp>,]<payload>` */
std::string encodeEvent(const std::string& nsp, const std::string& payload);

/** `3[<nsp>,]<ackId><payload>` */
std::string encodeAck(const std::string& nsp, const std::string& ackId,
                      const std::string& payload = std::string());

}  // namespace sio
}  // namespace oatpp_sio

#endif /* oatpp_sio_sio_wire_hpp */
