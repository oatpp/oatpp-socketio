#include "ConnectionCloseTest.hpp"

#include "RawWsClient.hpp"
#include "SioClient.hpp"
#include "TestAssert.hpp"
#include "TestConfig.hpp"

#include <string>

using namespace siotest;

void ConnectionCloseTest::onRun() {

  // ---------------------------------------------------------------------------
  // long-polling: a client-initiated engine.io CLOSE drops the session
  // ---------------------------------------------------------------------------
  {
    PollClient client(g_testPort);
    SIO_ASSERT(client.open());
    SIO_ASSERT(!client.closed());

    SIO_ASSERT(client.close());
    SIO_ASSERT(client.closed());

    // the server removed the sid, so the connection is gone for good
    SIO_ASSERT(client.state() == PollClient::ConnState::Closed);
    SIO_ASSERT(client.waitForClose(1000));
  }

  // ---------------------------------------------------------------------------
  // a probe reports the connection alive and hands over the message it read,
  // instead of swallowing it
  //
  // Exactly one probe here: the server gives a connection a single outstanding
  // long-poll and closes it if a second one arrives, so probing repeatedly
  // would tear the connection down ourselves and prove nothing.
  // ---------------------------------------------------------------------------
  {
    PollClient client(g_testPort);
    SIO_ASSERT(client.open());
    SIO_ASSERT(client.sioConnect("/"));

    PollClient other(g_testPort);
    SIO_ASSERT(other.open());
    SIO_ASSERT(other.sioConnect("/"));
    SIO_ASSERT(other.post("2[\"fromclose\",1]"));

    std::string delivered;
    SIO_ASSERT(client.state(&delivered) == PollClient::ConnState::Alive);
    SIO_ASSERT(delivered.find("2[\"fromclose\",1]") != std::string::npos);
    SIO_ASSERT(!client.closed());

    client.close();
    other.close();
  }

  // ---------------------------------------------------------------------------
  // websocket: the server closing the socket is observed by the client
  // ---------------------------------------------------------------------------
  {
    RawWsClient ws;
    SIO_ASSERT(ws.connect(g_testPort, "/socket.io/?EIO=4&transport=websocket"));

    std::string open;
    SIO_ASSERT(ws.receiveUntil("0", open));
    SIO_ASSERT(!ws.closed());

    // engine.io CLOSE; the server shuts the connection down
    SIO_ASSERT(ws.send("1"));
    SIO_ASSERT(ws.waitForClose(3000));
    SIO_ASSERT(ws.closed());
  }

  // ---------------------------------------------------------------------------
  // a socket.io DISCONNECT ("1") ends the namespace session but leaves the
  // engine.io connection alone - the two levels have separate lifecycles, and
  // conflating them would let one client disconnect another's transport
  // ---------------------------------------------------------------------------
  {
    PollClient client(g_testPort);
    SIO_ASSERT(client.open());
    SIO_ASSERT(client.sioConnect("/"));

    SIO_ASSERT(client.post("1"));  // socket.io DISCONNECT, not engine.io CLOSE

    std::string delivered;
    SIO_ASSERT(client.state(&delivered) == PollClient::ConnState::Alive);
    client.close();
  }
}
