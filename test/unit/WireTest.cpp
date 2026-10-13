#include "WireTest.hpp"

#include "TestAssert.hpp"

#include "oatpp_sio/sio/wire.hpp"

#include <string>
#include <vector>

using namespace oatpp_sio::sio;

namespace {

struct ParseCase {
  std::string input;    // packet content, without the type character
  bool ok;
  std::string attachments;
  std::string nsp;
  std::string ack;
  std::string payload;
};

}  // namespace

void WireTest::onRun() {

  // ---------------------------------------------------------------------------
  // parsing. Expected values match what the reference implementation
  // (socket.io-parser 4.x) produces for the same packets.
  // ---------------------------------------------------------------------------
  const std::vector<ParseCase> cases = {
      /* input,             ok, attachments, nsp,     ack,   payload */
      {"", true, "", "/", "", ""},
      {"{\"sid\":\"abc\"}", true, "", "/", "", "{\"sid\":\"abc\"}"},
      {"/chat,{\"sid\":\"abc\"}", true, "", "/chat", "", "{\"sid\":\"abc\"}"},
      {"/chat,12[\"msg\",\"hi\"]", true, "", "/chat", "12", "[\"msg\",\"hi\"]"},
      {"/chat,[\"msg\"]", true, "", "/chat", "", "[\"msg\"]"},
      // no namespace -> the default one
      {"12[\"reply\"]", true, "", "/", "12", "[\"reply\"]"},
      {"[\"a\",1]", true, "", "/", "", "[\"a\",1]"},
      // namespace without the separator comma (lenient)
      {"/chat[\"msg\"]", true, "", "/chat", "", "[\"msg\"]"},
      {"/chat", true, "", "/chat", "", ""},
      // binary attachment prefix (comes before the namespace, the packet type
      // 5/6 already says "binary")
      {"2-[\"x\"]", true, "2", "/", "", "[\"x\"]"},
      {"2-/chat,[\"x\"]", true, "2", "/chat", "", "[\"x\"]"},
      // digits that are not followed by '-' are an ack id, not attachments
      {"12-[\"x\"]", true, "12", "/", "", "[\"x\"]"},
      // malformed: a payload must be JSON
      {"x", false, "", "/", "", "x"},
      {"not json", false, "", "/", "", "not json"},
  };

  for (const auto& c : cases) {
    WirePacket p;
    const bool ok = parsePacket(c.input, p);
    if (ok != c.ok || p.nsp != c.nsp || p.ack != c.ack ||
        p.payload != c.payload || p.attachments != c.attachments) {
      ::siotest::fail("parsePacket(" + c.input + ")",
                      std::string("expected ok=") + (c.ok ? "1" : "0") +
                          " nsp=" + c.nsp + " ack=" + c.ack +
                          " attachments=" + c.attachments +
                          " payload=" + c.payload + "\n      got      ok=" +
                          (ok ? "1" : "0") + " nsp=" + p.nsp +
                          " ack=" + p.ack + " attachments=" + p.attachments +
                          " payload=" + p.payload);
    }
  }

  // ---------------------------------------------------------------------------
  // truncated / hostile input must not crash and must not read out of bounds
  // (the old parser indexed data[0] unconditionally and computed
  //  data.size() - 1 on a possibly empty string)
  // ---------------------------------------------------------------------------
  const std::vector<std::string> fuzzSeeds = {
      "",
      "-",
      "0",
      "0-",
      "/",
      "/,",
      "/chat",
      "/chat,",
      "/chat,12",
      "2-",
      "2-[",
      "[",
      "{",
      "x",
      "/x",
      "/x,",
      "12345678901234567890",
      "/,,,,,,,,,,",
      "-,-,-,-",
  };

  for (const auto& seed : fuzzSeeds) {
    for (size_t len = 0; len <= seed.size(); len++) {
      const std::string truncated = seed.substr(0, len);
      WirePacket p;
      // must not throw and must not touch memory outside the string
      parsePacket(truncated, p);
    }
  }

  // ---------------------------------------------------------------------------
  // encoding
  // ---------------------------------------------------------------------------
  SIO_ASSERT_EQ(encodeConnectAck("/", "abc"),
                std::string("0{\"sid\":\"abc\"}"));
  SIO_ASSERT_EQ(encodeConnectAck("/chat", "abc"),
                std::string("0/chat,{\"sid\":\"abc\"}"));

  SIO_ASSERT_EQ(encodeConnectError("/", "nope"),
                std::string("4{\"message\":\"nope\"}"));

  SIO_ASSERT_EQ(encodeDisconnect("/"), std::string("1"));
  SIO_ASSERT_EQ(encodeDisconnect("/chat"), std::string("1/chat,"));

  SIO_ASSERT_EQ(encodeEvent("/", "[\"a\",1]"), std::string("2[\"a\",1]"));
  SIO_ASSERT_EQ(encodeEvent("/chat", "[\"a\",1]"),
                std::string("2/chat,[\"a\",1]"));

  SIO_ASSERT_EQ(encodeAck("/", "12"), std::string("312"));
  SIO_ASSERT_EQ(encodeAck("/chat", "12", "[\"ok\"]"),
                std::string("3/chat,12[\"ok\"]"));

  // quotes in a sid / message are escaped, not injected into the JSON
  SIO_ASSERT_EQ(encodeConnectAck("/", "a\"b"),
                std::string("0{\"sid\":\"a\\\"b\"}"));

  // ---------------------------------------------------------------------------
  // round trip: encode -> parse must give the same packet back
  // ---------------------------------------------------------------------------
  const std::vector<std::pair<std::string, std::string>> roundTrip = {
      {"/", "[\"event\",{\"a\":1}]"},
      {"/chat", "[\"event\",2]"},
      {"/deep/ns", "[1,2,3]"},
  };

  for (const auto& rt : roundTrip) {
    const std::string encoded = encodeEvent(rt.first, rt.second);
    WirePacket p;
    SIO_ASSERT(parsePacket(encoded.substr(1), p));
    SIO_ASSERT_EQ(p.nsp, rt.first);
    SIO_ASSERT_EQ(p.payload, rt.second);
    SIO_ASSERT_EQ(p.ack, std::string(""));
  }
}
