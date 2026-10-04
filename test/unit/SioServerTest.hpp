/***************************************************************************
 *
 * Tests for oatpp_sio::sio::SioServer (space registry + subscriptions).
 *
 ***************************************************************************/

#ifndef SIO_TEST_SioServerTest_hpp
#define SIO_TEST_SioServerTest_hpp

#include "TestRunner.hpp"

/**
 * `SioServer` keeps the registry of spaces (socket.io namespaces) and
 * connects/disconnects `SpaceListener`s.
 */
class SioServerTest : public siotest::Test {
public:
  SioServerTest() : Test("TEST[sio.SioServer]") {}

  void onRun() override;
};

#endif /* SIO_TEST_SioServerTest_hpp */
