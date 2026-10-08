/***************************************************************************
 *
 * Tests for the socket.io v4 wire codec (oatpp_sio/sio/wire.hpp).
 *
 ***************************************************************************/

#ifndef SIO_TEST_WireTest_hpp
#define SIO_TEST_WireTest_hpp

#include "TestRunner.hpp"

class WireTest : public siotest::Test {
public:
  WireTest() : Test("TEST[sio.Wire]") {}

  void onRun() override;
};

#endif /* SIO_TEST_WireTest_hpp */
