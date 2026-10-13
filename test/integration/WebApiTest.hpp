/***************************************************************************
 *
 * Integration tests for the plain web api around the socket.io endpoints:
 * static content, path traversal, swagger.
 *
 ***************************************************************************/

#ifndef SIO_TEST_WebApiTest_hpp
#define SIO_TEST_WebApiTest_hpp

#include "IntegrationTest.hpp"

class WebApiTest : public siotest::IntegrationTest {
public:
  WebApiTest() : IntegrationTest("TEST[integration.WebApi]") {}

  void onRun() override;
};

#endif /* SIO_TEST_WebApiTest_hpp */
