/***************************************************************************
 *
 * Tests for oatpp_sio::sio::Space (namespace subscription + fan-out).
 *
 ***************************************************************************/

#ifndef SIO_TEST_SpaceTest_hpp
#define SIO_TEST_SpaceTest_hpp

#include "oatpp-test/UnitTest.hpp"

/**
 * `Space` is the socket.io namespace: a set of `SpaceListener`s plus
 * publish()/publishAsync() fan-out.
 */
class SpaceTest : public oatpp::test::UnitTest {
public:
  SpaceTest() : UnitTest("TEST[sio.Space]") {}

  void onRun() override;
};

#endif /* SIO_TEST_SpaceTest_hpp */
