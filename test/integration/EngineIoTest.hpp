/***************************************************************************
 *
 * Integration tests: engine.io over HTTP long-polling against the real
 * server (in-process web api).
 *
 ***************************************************************************/

#ifndef SIO_TEST_EngineIoTest_hpp
#define SIO_TEST_EngineIoTest_hpp

#include "IntegrationTest.hpp"

class EngineIoTest : public siotest::IntegrationTest {
public:
  EngineIoTest() : IntegrationTest("TEST[integration.EngineIo]") {}

  void onRun() override;
};

#endif /* SIO_TEST_EngineIoTest_hpp */
