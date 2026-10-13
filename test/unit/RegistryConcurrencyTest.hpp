/***************************************************************************
 *
 * The namespace registry under concurrent access: connects, drops and
 * lookups from several threads at once.
 *
 * The registry is a map that the request path writes to (auto-create) and
 * reads from, while the application declares and retires namespaces from
 * wherever it likes. This is the test that says "unsynchronised" is a bug
 * and not a style preference.
 *
 ***************************************************************************/

#ifndef SIO_TEST_RegistryConcurrencyTest_hpp
#define SIO_TEST_RegistryConcurrencyTest_hpp

#include "TestRunner.hpp"

class RegistryConcurrencyTest : public siotest::Test {
public:
  RegistryConcurrencyTest() : Test("RegistryConcurrencyTest") {}

  void onRun() override;
};

#endif /* SIO_TEST_RegistryConcurrencyTest_hpp */
