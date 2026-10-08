/***************************************************************************
 *
 * The application's auth plugin, exercised against the running server.
 *
 * The plugin interface itself is unit-tested (unit.AuthPluginTest); this test
 * checks the two things only an end-to-end run can show: that a refusal
 * reaches the client as a socket.io CONNECT_ERROR with the plugin's message,
 * and that a publish refusal actually stops the event from being delivered.
 *
 ***************************************************************************/

#ifndef SIO_TEST_AuthTest_hpp
#define SIO_TEST_AuthTest_hpp

#include "IntegrationTest.hpp"

class AuthTest : public siotest::IntegrationTest {
public:
  AuthTest() : IntegrationTest("TEST[integration.Auth]") {}

  void onRun() override;
};

#endif /* SIO_TEST_AuthTest_hpp */
