/***************************************************************************
 *
 * Tests for oatpp_sio::generateRandomString (session id generation).
 *
 ***************************************************************************/

#ifndef SIO_TEST_UtilTest_hpp
#define SIO_TEST_UtilTest_hpp

#include "oatpp-test/UnitTest.hpp"

class UtilTest : public oatpp::test::UnitTest {
public:
  UtilTest() : UnitTest("TEST[util]") {}

  void onRun() override;
};

#endif /* SIO_TEST_UtilTest_hpp */
