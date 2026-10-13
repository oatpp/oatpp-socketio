/***************************************************************************
 *
 * Integration tests: the websocket transport (engine.io + socket.io over a
 * real RFC 6455 connection).
 *
 ***************************************************************************/

#ifndef SIO_TEST_WebSocketTest_hpp
#define SIO_TEST_WebSocketTest_hpp

#include "IntegrationTest.hpp"

class WebSocketTest : public siotest::IntegrationTest {
public:
  WebSocketTest() : IntegrationTest("TEST[integration.WebSocket]") {}

  void onRun() override;
};

#endif /* SIO_TEST_WebSocketTest_hpp */
