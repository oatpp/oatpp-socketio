/***************************************************************************
 *
 * Integration tests: socket.io over HTTP long-polling (connect, events,
 * acks, namespaces).
 *
 ***************************************************************************/

#ifndef SIO_TEST_SocketIoTest_hpp
#define SIO_TEST_SocketIoTest_hpp

#include "IntegrationTest.hpp"

class SocketIoTest : public siotest::IntegrationTest {
public:
  SocketIoTest() : IntegrationTest("TEST[integration.SocketIo]") {}

  void onRun() override;
};

#endif /* SIO_TEST_SocketIoTest_hpp */
