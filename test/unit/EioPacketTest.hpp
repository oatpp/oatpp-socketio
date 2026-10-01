/***************************************************************************
 *
 * Tests for the engine.io packet framing (oatpp_sio/eio/packet.hpp).
 *
 ***************************************************************************/

#ifndef SIO_TEST_EioPacketTest_hpp
#define SIO_TEST_EioPacketTest_hpp

#include "oatpp-test/UnitTest.hpp"

class EioPacketTest : public oatpp::test::UnitTest {
public:
  EioPacketTest() : UnitTest("TEST[eio.Packet]") {}

  void onRun() override;
};

#endif /* SIO_TEST_EioPacketTest_hpp */
