#include "ConnectionCloseTest.hpp"

#include "RawHttpClient.hpp"
#include "RawWsClient.hpp"
#include "SioClient.hpp"
#include "TestAssert.hpp"
#include "TestConfig.hpp"

#include <chrono>
#include <string>
#include <thread>

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

  // ---------------------------------------------------------------------------
  // a closed session is really gone: it must not keep receiving broadcasts
  //
  // This is what keeps the suite order-independent. A long-poll session that
  // is merely abandoned stays in the engine until the ping timeout - minutes -
  // and every broadcast in a later test fans out to it as well.
  // ---------------------------------------------------------------------------
  {
    PollClient stale(g_testPort);
    SIO_ASSERT(stale.open());
    SIO_ASSERT(stale.sioConnect("/"));
    stale.close();

    PollClient sender(g_testPort);
    PollClient receiver(g_testPort);
    SIO_ASSERT(sender.open());
    SIO_ASSERT(sender.sioConnect("/"));
    SIO_ASSERT(receiver.open());
    SIO_ASSERT(receiver.sioConnect("/"));

    SIO_ASSERT(sender.post("2[\"isolation\",1]"));

    std::string got;
    SIO_ASSERT(receiver.state(&got) == PollClient::ConnState::Alive);
    SIO_ASSERT(got.find("2[\"isolation\",1]") != std::string::npos);

    // the closed one cannot even poll any more
    SIO_ASSERT(stale.state() == PollClient::ConnState::Closed);
  }

  // ---------------------------------------------------------------------------
  // a second poll while one is pending is refused, and surviving it is the
  // point
  //
  // A connection gets exactly one outstanding long-poll, so refusing the
  // overlap is correct. It used to also close the connection, which meant a
  // client that re-polled after its own client-side timeout destroyed its own
  // session - every client, eventually.
  // ---------------------------------------------------------------------------
  {
    PollClient client(g_testPort);
    SIO_ASSERT(client.open());
    SIO_ASSERT(client.sioConnect("/"));

    const std::string pollPath =
        "/socket.io/?EIO=4&transport=polling&sid=" + client.sid();

    // hold a poll open; nothing is queued, so the server keeps it
    std::thread holder([&] {
      httpRequest(g_testPort, "GET", pollPath, std::string(), 2000);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    const RawResponse overlap =
        httpRequest(g_testPort, "GET", pollPath, std::string(), 1000);
    SIO_ASSERT_EQ(overlap.status, 400);

    holder.join();

    // Liveness is checked with a POST, not a poll: the held poll is still
    // outstanding server-side, and that slot is what is under test here.
    SIO_ASSERT(client.post("2[\"afteroverlap\",1]"));
    client.close();
  }
}
