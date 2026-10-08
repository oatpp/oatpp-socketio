/***************************************************************************
 *
 * Integration tests: connection lifecycle / close observation.
 *
 * These pin the harness's ability to observe the server dropping a connection
 * (PollClient::state/waitForClose, RawWsClient::closed/waitForClose), which
 * the negative protocol tests need: "the server rejected that" is only a real
 * assertion if the client can tell a dropped connection from a quiet one.
 *
 ***************************************************************************/

#ifndef SIO_TEST_ConnectionCloseTest_hpp
#define SIO_TEST_ConnectionCloseTest_hpp

#include "IntegrationTest.hpp"

class ConnectionCloseTest : public siotest::IntegrationTest {
public:
  ConnectionCloseTest() : IntegrationTest("TEST[integration.Close]") {}

  void onRun() override;
};

#endif /* SIO_TEST_ConnectionCloseTest_hpp */
