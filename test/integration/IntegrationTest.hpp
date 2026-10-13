/***************************************************************************
 *
 * Base class for the integration tests.
 *
 * The web api is a process-wide singleton, so all integration tests share one
 * in-process server. Its connections, handlers and coroutines are still alive
 * when an individual test ends, which makes the oatpp object-count check
 * meaningless at this level (it would flag every test, always). The unit tests
 * keep the check on. See TestRunner.hpp for why this is not
 * oatpp::test::UnitTest.
 *
 * A test that owns everything it allocates - i.e. one that connects, does its
 * thing and disconnects again - can opt back in with checkLeaks(true).
 *
 ***************************************************************************/

#ifndef SIO_TEST_IntegrationTest_hpp
#define SIO_TEST_IntegrationTest_hpp

#include "TestRunner.hpp"

namespace siotest {

class IntegrationTest : public Test {
public:
  explicit IntegrationTest(const char* tag) : Test(tag) { checkLeaks(false); }
};

}  // namespace siotest

#endif /* SIO_TEST_IntegrationTest_hpp */
