#include "WebSocketTest.hpp"

#include "RawHttpClient.hpp"
#include "RawWsClient.hpp"
#include "SioClient.hpp"
#include "TestAssert.hpp"
#include "TestConfig.hpp"

#include <string>

using namespace siotest;

namespace {

std::string extractSid(const std::string& packet) {
  const std::string key = "\"sid\":\"";
  const size_t start = packet.find(key);
  if (start == std::string::npos) {
    return std::string();
  }
  const size_t from = start + key.size();
  const size_t end = packet.find('"', from);
  return end == std::string::npos ? std::string() : packet.substr(from, end - from);
}

}  // namespace

void WebSocketTest::onRun() {

  RawWsClient ws;
  SIO_ASSERT(ws.connect(g_testPort, "/socket.io/?EIO=4&transport=websocket"));

  // -- engine.io OPEN ------------------------------------------------------
  std::string open;
  SIO_ASSERT(ws.receiveUntil("0", open));
  SIO_ASSERT(open.find("\"sid\":") != std::string::npos);
  // a direct websocket connection has nothing to upgrade to
  SIO_ASSERT(open.find("\"upgrades\":[]") != std::string::npos);
  SIO_ASSERT(open.find("\"pingInterval\":25000") != std::string::npos);

  const std::string sid = extractSid(open);
  SIO_ASSERT(!sid.empty());

  // -- socket.io CONNECT ---------------------------------------------------
  SIO_ASSERT(ws.send("40"));
  std::string ack;
  SIO_ASSERT(ws.receiveUntil("40", ack));
  SIO_ASSERT(ack.find("\"sid\":") != std::string::npos);

  // -- a long-poll request for an upgraded connection is refused -----------
  {
    const RawResponse response = httpRequest(
        g_testPort, "GET", "/socket.io/?EIO=4&transport=polling&sid=" + sid);
    SIO_ASSERT_EQ(response.status, 400);
  }

  // -- websocket -> long-polling broadcast ---------------------------------
  {
    PollClient poller(g_testPort);
    SIO_ASSERT(poller.open());
    SIO_ASSERT(poller.sioConnect("/"));

    SIO_ASSERT(ws.send("42[\"fromws\",42]"));

    const std::string received = poller.poll();
    SIO_ASSERT(received.find("2[\"fromws\",42]") != std::string::npos);
  }

  // -- long-polling -> websocket broadcast ---------------------------------
  {
    PollClient sender(g_testPort);
    SIO_ASSERT(sender.open());
    SIO_ASSERT(sender.sioConnect("/"));

    SIO_ASSERT(sender.post("2[\"topoller\"]"));

    std::string received;
    SIO_ASSERT(ws.receiveUntil("42[\"topoller\"]", received));
  }
}
