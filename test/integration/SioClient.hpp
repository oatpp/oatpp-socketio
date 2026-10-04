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
   * @note This takes the connection's single long-poll slot. The server has no
   *       server-side long-poll timeout, so a probe that times out leaves the
   *       server still holding that request - and the server closes a
   *       connection that sends a second poll while one is outstanding. Issue
   *       at most one probe between messages, or use waitForClose().
   */
  ConnState state(std::string* delivered = nullptr, int timeoutMs = 1500);

  /**
   * Wait until the server closes the connection.
   * @return true if it did - the assertion a negative test wants.
   */
  bool waitForClose(int timeoutMs = 3000);

  /**
   * Send an engine.io CLOSE ("1"), which makes the server drop the connection
   * and forget the sid. Call at the end of a test that connected, so the next
   * one does not inherit the leftover session.
   */
  bool close();

  /** true once the connection was seen closed, or close() was called */
  bool closed() const { return m_closed; }

  const std::string& sid() const { return m_sid; }
  const std::string& sioSid() const { return m_sioSid; }
  const std::string& openPacket() const { return m_openPacket; }

private:
  unsigned short m_port;
  std::string m_sid;
  std::string m_sioSid;
  std::string m_openPacket;
  bool m_closed = false;
};

}  // namespace siotest

#endif /* SIO_TEST_SioClient_hpp */
