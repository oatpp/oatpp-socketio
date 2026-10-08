/***************************************************************************
 *
 * Tests for oatpp_sio::generateRandomString (session id generation).
 *
 ***************************************************************************/

#ifndef SIO_TEST_UtilTest_hpp
#define SIO_TEST_UtilTest_hpp

#include "TestRunner.hpp"

class UtilTest : public siotest::Test {
public:
  UtilTest() : Test("TEST[util]") {}

  void onRun() override;
};

#endif /* SIO_TEST_UtilTest_hpp */
