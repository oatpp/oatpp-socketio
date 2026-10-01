/***************************************************************************
 *
 * Minimal test runner.
 *
 * oatpp-test provides `oatpp::test::UnitTest` but no registry, so tests
 * register themselves here. This allows running the whole suite or single
 * tests by name - CTest registers one process per test, which keeps a hard
 * crash of one test from taking the others down.
 *
 *   <exe>                 run all tests
 *   <exe> UtilTest ...    run the named tests
 *   <exe> --list          list test names, one per line
 *
 ***************************************************************************/

#ifndef SIO_TEST_TestRunner_hpp
#define SIO_TEST_TestRunner_hpp

#include "oatpp-test/UnitTest.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace siotest {

struct TestEntry {
  std::string name;
  std::function<std::unique_ptr<oatpp::test::UnitTest>()> factory;
};

/** all registered tests (definition order) */
std::vector<TestEntry>& registry();

struct TestRegistrar {
  TestRegistrar(const std::string& name,
                std::function<std::unique_ptr<oatpp::test::UnitTest>()> factory);
};

/** run tests, returns the process exit code */
int runFromArgs(int argc, const char* argv[]);

}  // namespace siotest

/** register a UnitTest-derived class under the name @p NAME */
#define SIO_REGISTER_TEST(NAME, CLASS)                                       \
  static ::siotest::TestRegistrar s_testRegistrar_##CLASS(                   \
      #NAME, []() -> std::unique_ptr<oatpp::test::UnitTest> {               \
        return std::unique_ptr<oatpp::test::UnitTest>(new CLASS());         \
      })

#endif /* SIO_TEST_TestRunner_hpp */
