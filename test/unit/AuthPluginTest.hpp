/***************************************************************************
 *
 * The auth plugin hook: who may join a namespace, who may publish into one.
 *
 * No sockets - the plugin is called from SioServer::connectToSpace() and from
 * the adapter on publish, both of which are reachable directly.
 *
 ***************************************************************************/

#ifndef SIO_TEST_AuthPluginTest_hpp
#define SIO_TEST_AuthPluginTest_hpp

#include "TestRunner.hpp"

class AuthPluginTest : public siotest::Test {
public:
  AuthPluginTest() : Test("TEST[unit.AuthPlugin]") {}

  void onRun() override;
};

#endif /* SIO_TEST_AuthPluginTest_hpp */
