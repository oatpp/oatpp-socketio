/***************************************************************************
 *
 * Minimal WebSocket (RFC 6455) client for the integration tests.
 *
 * Text frames only, client frames masked - enough to drive the engine.io /
 * socket.io websocket transport without depending on a third party client.
 *
 ***************************************************************************/

#ifndef SIO_TEST_RawWsClient_hpp
#define SIO_TEST_RawWsClient_hpp

#include <string>

namespace siotest {

class RawWsClient {
public:
  ~RawWsClient();

  /**
   * TCP connect + HTTP Upgrade handshake.
   * @return false if the server did not answer with 101.
   */
  bool connect(unsigned short port, const std::string& path, int timeoutMs = 5000);

  /** send one masked text frame */
  bool send(const std::string& payload);

  /**
   * Read one frame.
   * @param payload out: payload of the frame
   * @param opcode out: 0x1 text, 0x8 close, 0x9 ping, 0xA pong
   * @return false on timeout or connection loss; a loss also sets closed(), a
   *         plain timeout does not.
   */
  bool receive(std::string& payload, unsigned int* opcode = nullptr,
               int timeoutMs = 3000);

  /**
   * Read frames until one whose payload starts with @p prefix arrives.
   * Stops early if the peer closes; false then means "never arrived", and
   * closed() says whether that was because the connection went away.
   */
  bool receiveUntil(const std::string& prefix, std::string& payload,
                    int timeoutMs = 5000);

  /**
   * True once the peer closed the connection, either with a close frame or by
   * hanging up (TCP FIN / reset). Sticky, so a test can assert on it after the
   * fact - this is how a test observes "the server dropped the connection".
   */
  bool closed() const { return m_closed; }

  /** true if the peer sent a proper close frame rather than just hanging up */
  bool closeFrameReceived() const { return m_closeFrame; }

  /** send a close frame (best effort), then close the socket */
  void sendClose();

  /**
   * Block until the peer closes the connection or the timeout expires.
   * Frames arriving in the meantime are discarded.
   * @return true if the close was observed.
   */
  bool waitForClose(int timeoutMs = 3000);

  void close();

  bool isConnected() const { return m_fd >= 0; }

private:
  int m_fd = -1;
  bool m_closed = false;
  bool m_closeFrame = false;
};

}  // namespace siotest

#endif /* SIO_TEST_RawWsClient_hpp */
