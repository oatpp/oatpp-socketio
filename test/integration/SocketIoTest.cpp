#include "SocketIoTest.hpp"

#include "RawHttpClient.hpp"
#include "SioClient.hpp"
#include "TestAssert.hpp"
#include "TestConfig.hpp"

#include <string>

using namespace siotest;

void SocketIoTest::onRun() {

  // -- connect on the default namespace ------------------------------------
  PollClient a(g_testPort);
  SIO_ASSERT(a.open());
  SIO_ASSERT(a.sioConnect("/"));
  SIO_ASSERT(!a.sioSid().empty());
  // the socket.io id is the id the space knows the connection under
  SIO_ASSERT_EQ(a.sioSid(), a.sid());

  PollClient b(g_testPort);
  SIO_ASSERT(b.open());
  SIO_ASSERT(b.sioConnect("/"));
  SIO_ASSERT(b.sioSid() != a.sioSid());

  // -- broadcast: an event from A reaches B --------------------------------
  // (both are in the default namespace; the sender is filtered out)
  SIO_ASSERT(a.post("2[\"msg\",\"hello\"]"));

  const std::string received = b.poll();
  SIO_ASSERT(received.find("2[\"msg\",\"hello\"]") != std::string::npos);
  // engine.io framing: "4" (MESSAGE) + "2" (EVENT) + payload
  SIO_ASSERT(startsWith(received, "42"));

  // -- acks ----------------------------------------------------------------
  {
    PollClient c(g_testPort);
    SIO_ASSERT(c.open());
    SIO_ASSERT(c.sioConnect("/"));

    // an event with ack id 12 must be acked with "3" + "12"
    SIO_ASSERT(c.post("212[\"withack\"]"));
    const std::string answer = c.poll();
    SIO_ASSERT_EQ(answer, std::string("4312"));
  }

  // -- a malformed event is dropped, not acked, not published --------------
  {
    PollClient c(g_testPort);
    SIO_ASSERT(c.open());
    SIO_ASSERT(c.sioConnect("/"));

    // "garbage" is not a JSON payload -> the server must not answer with the
    // "3" ack frame it used to send for every event
    SIO_ASSERT(c.post("2garbage"));
    // a valid event with an ack id right after it
    SIO_ASSERT(c.post("212[\"ok\"]"));

    // the only frame in the queue is the ack of the valid event; had the
    // malformed one been acked too, this would start with "43"
    const std::string answer = c.poll();
    SIO_ASSERT_EQ(answer, std::string("4312"));
  }

  // -- namespaces ----------------------------------------------------------
  {
    PollClient c(g_testPort);
    SIO_ASSERT(c.open());
    SIO_ASSERT(c.post("0/chat,"));
    const std::string ack = c.poll();
    SIO_ASSERT(startsWith(ack, "40/chat,"));
    SIO_ASSERT(ack.find("\"sid\":") != std::string::npos);
  }
  {
    // the reference client always writes the comma, but `0/chat` is valid
    // too and must not be refused
    PollClient c(g_testPort);
    SIO_ASSERT(c.open());
    SIO_ASSERT(c.post("0/rooms"));
    const std::string ack = c.poll();
    SIO_ASSERT(startsWith(ack, "40/rooms,"));
  }

  // -- an event on a namespace stays on that namespace ---------------------
  {
    PollClient inChat(g_testPort);
    SIO_ASSERT(inChat.open());
    SIO_ASSERT(inChat.sioConnect("/chat"));

    PollClient other(g_testPort);
    SIO_ASSERT(other.open());
    SIO_ASSERT(other.sioConnect("/chat"));

    // the namespace is part of every packet on a non-root namespace - that
    // field is how one connection multiplexes several namespaces, so a client
    // does not send a bare "2[...]" here. (Sent unprefixed, the packet decodes
    // to the root namespace and this test silently checks the wrong thing.)
    SIO_ASSERT(inChat.post("2/chat,[\"chatmsg\",1]"));
    const std::string received = other.poll();
    SIO_ASSERT(received.find("2/chat,[\"chatmsg\",1]") != std::string::npos);
  }

  // -- an empty packet does not kill the connection ------------------------
  {
    PollClient c(g_testPort);
    SIO_ASSERT(c.open());
    // engine.io MESSAGE with an empty body -> no socket.io packet type
    const RawResponse response = httpRequest(
        g_testPort, "POST", "/socket.io/?EIO=4&transport=polling&sid=" + c.sid(), "4");
    SIO_ASSERT_EQ(response.status, 200);

    // the connection still works
    SIO_ASSERT(c.sioConnect("/"));
  }
}
