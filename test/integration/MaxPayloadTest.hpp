/***************************************************************************
 *
 * maxPayload is advertised in the engine.io OPEN packet - this checks that
 * it is also enforced, on both transports.
 *
 ***************************************************************************/

#ifndef SIO_TEST_MaxPayloadTest_hpp
#define SIO_TEST_MaxPayloadTest_hpp

#include "IntegrationTest.hpp"

class MaxPayloadTest : public siotest::IntegrationTest {
public:
  MaxPayloadTest() : IntegrationTest("MaxPayloadTest") {}

  void onRun() override;
};

#endif /* SIO_TEST_MaxPayloadTest_hpp */
