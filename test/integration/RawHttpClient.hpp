/***************************************************************************
 *
 * Minimal HTTP/1.1 client for the integration tests.
 *
 * Deliberately not oatpp's HttpRequestExecutor: the tests have to send
 * non-normalized paths (`/../../etc/passwd`) and have to observe that a
 * request does *not* get a response (long-poll semantics), which needs a
 * socket read timeout and full control over the request line.
 *
 ***************************************************************************/

#ifndef SIO_TEST_RawHttpClient_hpp
#define SIO_TEST_RawHttpClient_hpp

#include <string>

namespace siotest {

/** true if @p text starts with @p prefix */
inline bool startsWith(const std::string& text, const std::string& prefix) {
  return text.size() >= prefix.size() &&
         text.compare(0, prefix.size(), prefix) == 0;
}

struct RawResponse {
  /** false if the connection could not be established or the response was
   *  malformed */
  bool ok = false;
  /** true if the socket timed out before the response was complete.
   *  For a long-poll GET a timeout means the server is still holding the
   *  request open, i.e. the connection is alive. */
  bool timedOut = false;
  /** the TCP connection could not be established at all (nothing listening,
   *  refused). No request was sent. */
  bool connectFailed = false;
  /** the peer hung up without sending a complete response.
   *
   * For a long-poll GET this is how a client observes the server dropping the
   * connection; contrast with timedOut, which means it is still up. */
  bool peerClosed = false;
  int status = 0;
  std::string body;
  std::string headers;
};

/**
 * Send one HTTP request and read the response.
 *
 * @param method e.g. "GET".
 * @param path request target, sent verbatim (no normalization, no encoding).
 * @param body request body, empty for none.
 * @param timeoutMs socket timeout for connect/read.
 */
RawResponse httpRequest(unsigned short port, const std::string& method,
                        const std::string& path,
                        const std::string& body = std::string(),
                        int timeoutMs = 5000);

/**
 * POST a body with neither Content-Length nor Transfer-Encoding, then shut the
 * write side of the connection down so the body is delimited by end-of-file.
 *
 * This is how a test reaches a server's check on the size that *actually*
 * arrived: lying with a small Content-Length does not work, because an HTTP
 * reader honours the header and reads exactly that many bytes. The read side
 * stays open, so the response can still be read after the half-close.
 */
RawResponse httpRequestBodyToEof(unsigned short port, const std::string& path,
                                 const std::string& body, int timeoutMs = 5000);

/**
 * Send request headers that promise a body of @p declaredLength bytes and then
 * simply do not send it.
 *
 * A server that measures the body after reading it can only answer once the
 * body arrived; one that checks the declared length first answers straight
 * away. So the difference between the two is visible in *time*: this request
 * gets a response only if the server looked at the header. Sending a real body
 * would not tell them apart, because both checks reject it.
 */
RawResponse httpRequestHeadersOnly(unsigned short port, const std::string& path,
                                   size_t declaredLength, int timeoutMs = 2000);

/** true once something accepts TCP connections on port */
bool waitForPort(unsigned short port, int timeoutMs = 10000);

}  // namespace siotest

#endif /* SIO_TEST_RawHttpClient_hpp */
