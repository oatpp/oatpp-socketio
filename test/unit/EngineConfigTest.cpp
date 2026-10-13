#include "EngineConfigTest.hpp"

#include "TestAssert.hpp"

#include "oatpp_sio/eio/engineIo.hpp"

using namespace oatpp_sio::eio;

void EngineConfigTest::onRun() {

  // The engine has one global instance, created with the library.
  SIO_ASSERT(theEngine != nullptr);

  // pingInterval/pingTimeout are milliseconds - they are put verbatim into the
  // OPEN packet and passed to waitRepeat(std::chrono::milliseconds(...)).
  // They used to default to 300*1000/200*1000 while the demo apps passed
  // 300/200, which made the server ping every 300ms.
  SIO_ASSERT_EQ(theEngine->pingInterval, 25000u);
  SIO_ASSERT_EQ(theEngine->pingTimeout, 20000u);
  SIO_ASSERT_EQ(theEngine->maxPayload, 1000000u);

  // setConfig() takes the same units
  const unsigned int savedInterval = theEngine->pingInterval;
  const unsigned int savedTimeout = theEngine->pingTimeout;
  const unsigned int savedMax = theEngine->maxPayload;

  theEngine->setConfig(11000, 10500, 42);
  SIO_ASSERT_EQ(theEngine->pingInterval, 11000u);
  SIO_ASSERT_EQ(theEngine->pingTimeout, 10500u);
  SIO_ASSERT_EQ(theEngine->maxPayload, 42u);

  theEngine->setConfig(savedInterval, savedTimeout, savedMax);
  SIO_ASSERT_EQ(theEngine->pingInterval, savedInterval);
  SIO_ASSERT_EQ(theEngine->pingTimeout, savedTimeout);
  SIO_ASSERT_EQ(theEngine->maxPayload, savedMax);
}
