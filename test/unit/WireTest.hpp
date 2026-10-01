/***************************************************************************
 *
 * Tests for the socket.io v4 wire codec (oatpp_sio/sio/wire.hpp).
 *
 ***************************************************************************/

#ifndef SIO_TEST_WireTest_hpp
#define SIO_TEST_WireTest_hpp

#include "oatpp-test/UnitTest.hpp"

class WireTest : public oatpp::test::UnitTest {
public:
  WireTest() : UnitTest("TEST[sio.Wire]") {}

  void onRun() override;
};

#endif /* SIO_TEST_WireTest_hpp */
