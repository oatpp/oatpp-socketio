#include "EioPacketTest.hpp"

#include "TestAssert.hpp"

#include "oatpp_sio/eio/packet.hpp"

#include <string>

using namespace oatpp_sio::eio;

void EioPacketTest::onRun() {

  // -- wire type characters, see https://socket.io/docs/v4/engine-io-protocol/
  SIO_ASSERT_EQ((int)eioOpen, (int)'0');
  SIO_ASSERT_EQ((int)eioClose, (int)'1');
  SIO_ASSERT_EQ((int)eioPing, (int)'2');
  SIO_ASSERT_EQ((int)eioPong, (int)'3');
  SIO_ASSERT_EQ((int)eioMessage, (int)'4');
  SIO_ASSERT_EQ((int)eiouUgrade, (int)'5');
  SIO_ASSERT_EQ((int)eioNoop, (int)'6');
  SIO_ASSERT_EQ((int)eioBinary, (int)'b');

  // -- encoding --------------------------------------------------------------
  SIO_ASSERT_EQ(pktEncode(eioOpen, ""), std::string("0"));
  SIO_ASSERT_EQ(pktEncode(eioClose, ""), std::string("1"));
  SIO_ASSERT_EQ(pktEncode(eioPing, ""), std::string("2"));
  SIO_ASSERT_EQ(pktEncode(eioPong, ""), std::string("3"));
  SIO_ASSERT_EQ(pktEncode(eioNoop, ""), std::string("6"));

  // a socket.io CONNECT wrapped into an engine.io MESSAGE
  SIO_ASSERT_EQ(pktEncode(eioMessage, "40"), std::string("440"));
  SIO_ASSERT_EQ(pktEncode(eioMessage, std::string("40{\"sid\":\"abc\"}")),
                std::string("40{\"sid\":\"abc\"}").insert(0, "4"));

  // -- encoding is binary safe (payload may contain '\0') --------------------
  const std::string bin("a\0b", 3);
  const std::string enc = pktEncode(eioBinary, bin);
  SIO_ASSERT_EQ(enc.size(), size_t(4));
  SIO_ASSERT_EQ(enc, std::string("ba\0b", 4));

  // -- decoding --------------------------------------------------------------
  SIO_ASSERT_EQ(pktType("0abc"), '0');
  SIO_ASSERT_EQ(pktType("4"), '4');
  SIO_ASSERT_EQ(pktType(""), '\0'); // empty packet: no type, no crash

  SIO_ASSERT_EQ(pktPayload("0abc"), std::string("abc"));
  SIO_ASSERT_EQ(pktPayload("2"), std::string(""));
  SIO_ASSERT_EQ(pktPayload(""), std::string(""));

  // payload with embedded NULs
  const std::string raw("4a\0b", 4);
  SIO_ASSERT_EQ(pktType(raw), '4');
  SIO_ASSERT_EQ(pktPayload(raw), std::string("a\0b", 3));
}
