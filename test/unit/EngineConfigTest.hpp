/***************************************************************************
 *
 * Tests for the engine.io defaults (ping interval/timeout units).
 *
 ***************************************************************************/

#ifndef SIO_TEST_EngineConfigTest_hpp
#define SIO_TEST_EngineConfigTest_hpp

#include "oatpp-test/UnitTest.hpp"

class EngineConfigTest : public oatpp::test::UnitTest {
public:
  EngineConfigTest() : UnitTest("TEST[eio.Config]") {}

  void onRun() override;
};

#endif /* SIO_TEST_EngineConfigTest_hpp */
