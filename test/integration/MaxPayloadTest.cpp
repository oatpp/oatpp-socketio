#include "MaxPayloadTest.hpp"

#include "RawHttpClient.hpp"
#include "RawWsClient.hpp"
#include "SioClient.hpp"
#include "TestAssert.hpp"
#include "TestConfig.hpp"

#include "oatpp_sio/eio/engineIo.hpp"

#include <string>

using namespace siotest;
using oatpp_sio::eio::theEngine;

namespace {

/** the limit this test runs against - small enough to send past by hand */
const unsigned int kLimit = 64;

/**
 * Lowers maxPayload for the duration of the test and puts the engine config
 * back afterwards.
 *
 * The engine is process-wide state and the other integration tests assert the
 * advertised values (EngineIoTest checks for "maxPayload":1000000), so this
 * has to be restored even when the test throws.
 */
class MaxPayloadGuard {
public:
  explicit MaxPayloadGuard(unsigned int limit)
      : m_interval(theEngine->pingInterval), m_timeout(theEngine->pingTimeout),
        m_max(theEngine->maxPayload) {
    theEngine->setConfig(m_interval, m_timeout, limit);
  }
  ~MaxPayloadGuard() { theEngine->setConfig(m_interval, m_timeout, m_max); }

private:
  unsigned int m_interval;
  unsigned int m_timeout;
  unsigned int m_max;
};

std::string postPath(const std::string& sid) {
  return "/socket.io/?EIO=4&transport=polling&sid=" + sid;
}

/**
 * An engine.io MESSAGE holding a socket.io event, padded to exactly @p size
 * bytes.
 *
 * The body has to be something the engine accepts, because the point is to get
 * past the size check and still have a session for the next request: a body of
 * random bytes is a valid *size*, but it is an unknown engine.io packet type,
 * and the engine closes the connection over that.
 */
std::string packetOfExactSize(size_t size) {
  const std::string head = "42/chat,[\"p\",\"";
  const std::string tail = "\"]";
  if (size <= head.size() + tail.size()) {
    return std::string(size, 'x');
  }
  return head + std::string(size - head.size() - tail.size(), 'x') + tail;
}

}  // namespace

void MaxPayloadTest::onRun() {

  MaxPayloadGuard guard(kLimit);
  SIO_ASSERT_EQ(theEngine->maxPayload, kLimit);

  // -- long polling --------------------------------------------------------
  {
    PollClient c(g_testPort);
    SIO_ASSERT(c.open());
    SIO_ASSERT(c.sioConnect("/chat"));

    // over the limit: refused, and refused before the body was read
    const std::string tooBig = packetOfExactSize(200);
    const RawResponse rejected =
        httpRequest(g_testPort, "POST", postPath(c.sid()), tooBig);
    SIO_ASSERT_MSG(rejected.ok,
                   "expected a 413 answer, got status " +
                       std::to_string(rejected.status));
    SIO_ASSERT_EQ(rejected.status, 413);

    // exactly at the limit is not over it
    const std::string exactly = packetOfExactSize(kLimit);
    SIO_ASSERT_EQ(exactly.size(), size_t(kLimit));
    const RawResponse accepted =
        httpRequest(g_testPort, "POST", postPath(c.sid()), exactly);
    SIO_ASSERT(accepted.ok);
    SIO_ASSERT_EQ(accepted.status, 200);

    // one byte more is - and the session is still there to notice, which is
    // what makes this a size check and not a "the connection is gone" 400
    const RawResponse oneMore =
        httpRequest(g_testPort, "POST", postPath(c.sid()),
                    packetOfExactSize(kLimit + 1));
    SIO_ASSERT(oneMore.ok);
    SIO_ASSERT_EQ(oneMore.status, 413);

    // the session survives all of that: an oversized body is not a protocol
    // violation on the connection, and the client may keep talking
    SIO_ASSERT(c.post("2/chat,[\"small\",1]"));
    SIO_ASSERT(c.state() == PollClient::ConnState::Alive);
  }

  // -- a request that declares nothing is judged on what arrives -----------
  // Content-Length is a claim, and checking it is only half the job: a body
  // that arrives without one has to be measured after the fact.
  {
    PollClient c(g_testPort);
    SIO_ASSERT(c.open());

    const RawResponse response = httpRequestBodyToEof(
        g_testPort, postPath(c.sid()), packetOfExactSize(kLimit + 1));
    SIO_ASSERT_MSG(response.ok,
                   "expected an answer to a body-delimited POST, got status " +
                       std::to_string(response.status));
    SIO_ASSERT_EQ(response.status, 413);
  }

  // -- an oversized body is refused before it is read ----------------------
  // The point of checking the declared length is that the body never has to be
  // buffered, so the observable difference is that a client which declares a
  // body and never sends it still gets its 413. A server that only measures
  // what arrived would sit and wait for it, and this request would time out.
  {
    PollClient c(g_testPort);
    SIO_ASSERT(c.open());

    const RawResponse response =
        httpRequestHeadersOnly(g_testPort, postPath(c.sid()), kLimit + 1);
    SIO_ASSERT_MSG(response.ok && !response.timedOut,
                   "a declared body over the limit must be refused without "
                   "waiting for it (status " +
                       std::to_string(response.status) + ")");
    SIO_ASSERT_EQ(response.status, 413);
  }

  // -- websocket -----------------------------------------------------------
  {
    RawWsClient ws;
    SIO_ASSERT(ws.connect(g_testPort, "/socket.io/?EIO=4&transport=websocket"));

    std::string open;
    SIO_ASSERT(ws.receiveUntil("0", open));

    SIO_ASSERT(ws.send("40"));  // socket.io CONNECT on the root namespace
    std::string ack;
    SIO_ASSERT(ws.receiveUntil("40", ack));

    // join /chat, so the oversized packet below is one this connection is
    // entitled to send and the only thing wrong with it is its size
    SIO_ASSERT(ws.send("40/chat,"));
    std::string chatAck;
    SIO_ASSERT(ws.receiveUntil("40/chat", chatAck));

    // An oversized frame is dropped and the connection goes with it: the
    // alternative is to buffer a message of the client's choosing, or to
    // deliver half of one. The reference closes the socket here too.
    //
    // The packet is deliberately well-formed - a garbage frame would get the
    // connection closed by the protocol handling instead, and the test would
    // pass without the size check being what did it.
    SIO_ASSERT(ws.send(packetOfExactSize(200)));
    SIO_ASSERT_MSG(ws.waitForClose(3000),
                   "an oversized websocket message must close the connection");
  }
}
