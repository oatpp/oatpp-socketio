/***************************************************************************
 *
 * Tests for the engine.io packet framing (oatpp_sio/eio/packet.hpp).
 *
 ***************************************************************************/

#ifndef SIO_TEST_EioPacketTest_hpp
#define SIO_TEST_EioPacketTest_hpp

#include "TestRunner.hpp"

class EioPacketTest : public siotest::Test {
public:
  EioPacketTest() : Test("TEST[eio.Packet]") {}

  void onRun() override;
};

#endif /* SIO_TEST_EioPacketTest_hpp */
