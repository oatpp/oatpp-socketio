#include "EngineIoTest.hpp"

#include "RawHttpClient.hpp"
#include "SioClient.hpp"
#include "TestAssert.hpp"
#include "TestConfig.hpp"

#include <string>

using namespace siotest;

void EngineIoTest::onRun() {

  // -- handshake -----------------------------------------------------------
  PollClient client(g_testPort);
  SIO_ASSERT(client.open());

  // the sid is "sid_" + random
  SIO_ASSERT(client.sid().size() > 4);
  SIO_ASSERT(client.sid().compare(0, 4, "sid_") == 0);

  const std::string& open = client.openPacket();
  SIO_ASSERT(!open.empty());
  SIO_ASSERT_EQ(open[0], '0'); // engine.io OPEN
  SIO_ASSERT(open.find("\"upgrades\":[\"websocket\"]") != std::string::npos);
  // pingInterval/pingTimeout are milliseconds; a regression to the old
  // 300ms default would show up here
  SIO_ASSERT(open.find("\"pingInterval\":25000") != std::string::npos);
  SIO_ASSERT(open.find("\"pingTimeout\":20000") != std::string::npos);
  SIO_ASSERT(open.find("\"maxPayload\":1000000") != std::string::npos);

  // the same handshake on the /engine.io prefix
  {
    const RawResponse response =
        httpRequest(g_testPort, "GET", "/engine.io/?EIO=4&transport=polling");
    SIO_ASSERT_EQ(response.status, 200);
    SIO_ASSERT(response.body.find("\"sid\":") != std::string::npos);
  }
  {
    const RawResponse response =
        httpRequest(g_testPort, "GET", "/sio/?EIO=4&transport=polling");
    SIO_ASSERT_EQ(response.status, 200);
  }

  // -- protocol validation -------------------------------------------------
  struct {
    const char* method;
    std::string path;
    std::string body;
    int expected;
    const char* what;
  } cases[] = {
      {"GET", "/socket.io/?transport=polling", "", 400, "missing EIO"},
      {"GET", "/socket.io/?EIO=3&transport=polling", "", 400,
       "wrong EIO version"},
      {"GET", "/socket.io/?EIO=4", "", 400, "missing transport"},
      {"GET", "/socket.io/?EIO=4&transport=carrier-pigeon", "", 400,
       "unknown transport"},
      {"GET", "/socket.io/?EIO=4&transport=polling&sid=does-not-exist", "",
       400, "unknown sid"},
      {"POST", "/socket.io/?EIO=4&transport=polling", "40", 400,
       "POST without sid"},
      {"POST", "/socket.io/?EIO=4&transport=polling&sid=does-not-exist", "40",
       400, "POST with unknown sid"},
      {"POST", "/socket.io/?EIO=3&transport=polling&sid=x", "40", 400,
       "POST with wrong EIO"},
      {"PUT", "/socket.io/?EIO=4&transport=polling&sid=x", "", 400,
       "PUT is rejected"},
  };

  for (const auto& c : cases) {
    const RawResponse response =
        httpRequest(g_testPort, c.method, c.path, c.body);
    if (response.status != c.expected) {
      ::siotest::fail(std::string(c.what) + ": " + c.method + " " + c.path,
                      "expected HTTP " + std::to_string(c.expected) + ", got " +
                          std::to_string(response.status) +
                          (response.timedOut ? " (timed out)" : ""));
    }
  }

  // -- a POST with an empty body is rejected -------------------------------
  {
    const RawResponse response = httpRequest(
        g_testPort, "POST", "/socket.io/?EIO=4&transport=polling&sid=" + client.sid(), "");
    SIO_ASSERT_EQ(response.status, 400);
  }

  // -- two connections get different sids ----------------------------------
  {
    PollClient other(g_testPort);
    SIO_ASSERT(other.open());
    SIO_ASSERT(other.sid() != client.sid());
  }
}
