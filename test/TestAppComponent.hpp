/***************************************************************************
 *
 * Components the library expects to find in the oatpp environment.
 *
 * `Space` and `EioConnection` pull an Executor labelled "ws" through
 * OATPP_COMPONENT, so every test binary has to provide one.
 *
 ***************************************************************************/

#ifndef SIO_TEST_TestAppComponent_hpp
#define SIO_TEST_TestAppComponent_hpp

#include "oatpp/async/Executor.hpp"
#include "oatpp/macro/component.hpp"

#include <memory>

class TestAppComponent {
public:
  OATPP_CREATE_COMPONENT(std::shared_ptr<oatpp::async::Executor>, executorWs)
  ("ws", []() {
     return std::make_shared<oatpp::async::Executor>(1, 1, 1);
   }());
};

#endif /* SIO_TEST_TestAppComponent_hpp */
