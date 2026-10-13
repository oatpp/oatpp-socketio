/***************************************************************************
 *
 * Tests for the engine.io defaults (ping interval/timeout units).
 *
 ***************************************************************************/

#ifndef SIO_TEST_EngineConfigTest_hpp
#define SIO_TEST_EngineConfigTest_hpp

#include "TestRunner.hpp"

class EngineConfigTest : public siotest::Test {
public:
  EngineConfigTest() : Test("TEST[eio.Config]") {}

  void onRun() override;
};

#endif /* SIO_TEST_EngineConfigTest_hpp */
