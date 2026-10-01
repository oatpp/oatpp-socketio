/***************************************************************************
 *
 * oatpp-socketio unit test driver.
 *
 * Unit tests do not open sockets: they exercise the protocol building blocks
 * (framing, wire codec, spaces, session ids) in isolation.
 *
 ***************************************************************************/

#include "TestAppComponent.hpp"
#include "TestRunner.hpp"

#include "oatpp/Environment.hpp"
#include "oatpp/async/Executor.hpp"
#include "oatpp/macro/component.hpp"

#include "unit/EioPacketTest.hpp"
#include "unit/SioServerTest.hpp"
#include "unit/SpaceTest.hpp"
#include "unit/UtilTest.hpp"
#include "unit/WireTest.hpp"

SIO_REGISTER_TEST(EioPacketTest, EioPacketTest);
SIO_REGISTER_TEST(SioServerTest, SioServerTest);
SIO_REGISTER_TEST(SpaceTest, SpaceTest);
SIO_REGISTER_TEST(UtilTest, UtilTest);
SIO_REGISTER_TEST(WireTest, WireTest);

int main(int argc, const char* argv[]) {

  oatpp::Environment::init();

  int result;
  {
    TestAppComponent components; // provides the "ws" executor
    result = siotest::runFromArgs(argc, argv);

    // stop() before join(): join() on a running executor blocks forever and
    // ~Executor() on a joinable thread calls std::terminate()
    OATPP_COMPONENT(std::shared_ptr<oatpp::async::Executor>, executor, "ws");
    executor->stop();
    executor->join();
  }

  oatpp::Environment::destroy();
  return result;
}
