/***************************************************************************
 *
 * engine.io / socket.io client helpers for the integration tests: the
 * long-polling handshake and packet exchange the real clients do.
 *
 ***************************************************************************/

#ifndef SIO_TEST_SioClient_hpp
#define SIO_TEST_SioClient_hpp

#include "RawHttpClient.hpp"

#include <string>

namespace siotest {

/**
 * A socket.io client speaking engine.io long-polling on one port.
 *
 * Also models the connection lifecycle: close() sends an engine.io CLOSE so a
 * test can clean up after itself, and state()/waitForClose() let a test
 * observe the server dropping the connection.
 */
class PollClient {
public:
  explicit PollClient(unsigned short port) : m_port(port) {}

  /**
   * Sends an engine.io CLOSE unless close() was already called, so a test
   * cannot leak a session into the next one. Long-polling has no socket for
   * the server to notice a disconnect on, so without this the engine keeps the
   * sid (and the space keeps its subscription) until the ping timeout - minutes
   * - and every later broadcast fans out to the stale session too. Opt out with
   * autoClose(false).
   */
  ~PollClient();

  /** engine.io handshake (GET without sid). @return false on failure */
  bool open();

  /** socket.io CONNECT (packet "4<nsp>"), waits for the connect ack */
  bool sioConnect(const std::string& nsp = "/");

  /**
   * Send one socket.io packet. This is the complete socket.io packet,
   * *including* its type character - e.g. "2[\"evt\",1]" for an event, "0/chat,"
   * for a namespace connect. Only the engine.io "4" (MESSAGE) wrapper is added
   * here; the socket.io layer is the caller's responsibility.
   */
  bool post(const std::string& sioPacket);

  /** long-poll once; @return the response body (engine.io framed) */
  std::string poll(int timeoutMs = 3000);

  /**
   * Whether the engine.io connection is still there.
   *
   * Long-polling has no push channel to detect a dead connection with, so the
   * states are told apart by what a poll answers:
   *   - the request is held open until it times out -> Alive
   *   - a body comes back                           -> Alive
   *   - the peer hangs up                           -> Closed
   *   - the sid is no longer known (HTTP 400)       -> Closed
   *   - an engine.io CLOSE ("1")                    -> Closed
   */
  enum class ConnState { Alive, Closed };

  /**
   * Probe the connection with one poll.
   * @param delivered optional out: body of a successful poll, so that probing
   *        does not silently swallow a queued message.
   *
   * @note A connection has one outstanding long-poll slot. A probe that times
   *       out leaves the server still holding that request until it ends the
   *       response (now bounded by pingInterval), and a poll sent meanwhile is
   *       refused with 400 - the connection survives, but the probe reads
   *       nothing. Prefer waitForClose() when waiting for the server to act.
   */
  ConnState state(std::string* delivered = nullptr, int timeoutMs = 1500);

  /**
   * Wait until the server closes the connection.
   * @return true if it did - the assertion a negative test wants.
   */
  bool waitForClose(int timeoutMs = 3000);

  /**
   * Send an engine.io CLOSE ("1"), which makes the server drop the connection
   * and forget the sid. Best effort and non-throwing: it fails quietly if the
   * server is already gone, which is the normal outcome in a destructor.
   */
  bool close(int timeoutMs = 2000);

  /** true once the connection was seen closed, or close() was called */
  bool closed() const { return m_closed; }

  /** let the destructor close the session for you (default: true) */
  void autoClose(bool enabled) { m_autoClose = enabled; }

  const std::string& sid() const { return m_sid; }
  const std::string& sioSid() const { return m_sioSid; }
  const std::string& openPacket() const { return m_openPacket; }

private:
  unsigned short m_port;
  std::string m_sid;
  std::string m_sioSid;
  std::string m_openPacket;
  bool m_closed = false;
  bool m_autoClose = true;
};

}  // namespace siotest

#endif /* SIO_TEST_SioClient_hpp */
