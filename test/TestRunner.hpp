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

#include "oatpp/Environment.hpp"

#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace siotest {

/** thrown by a test that left oatpp objects behind (see Test::checkLeaks) */
class LeakError : public std::runtime_error {
public:
  explicit LeakError(const std::string& what) : std::runtime_error(what) {}
};

/**
 * Base class for tests.
 *
 * Deliberately not `oatpp::test::UnitTest`: its `run()` is non-virtual and
 * calls `exit(EXIT_FAILURE)` as soon as the global object count is higher at
 * the end of a test than at the start. That works for oatpp's own
 * one-binary-runs-everything harness, but here it is fatal - the integration
 * tests share a server that is still running, whose connections, handlers and
 * coroutines are legitimately alive when a test ends, so *every* integration
 * test hard-exited the process inside the first one. No PASS/FAIL was ever
 * reported and the remaining tests never ran.
 *
 * This `run()` reports instead of exiting: a failing leak check throws
 * LeakError, which the runner turns into a normal FAIL.
 *
 * Leak checking is per test and on by default, because that is the contract
 * the unit tests are written against (they allocate nothing that outlives
 * `onRun()`). Tests that share long-lived state - i.e. anything talking to the
 * in-process server - turn it off in their constructor and say why.
 */
class Test {
protected:
  const char* const TAG;

private:
  bool m_checkLeaks = true;

public:
  explicit Test(const char* testTag) : TAG(testTag) {}
  virtual ~Test() = default;

  /** enable/disable the oatpp object-count check for this test */
  void checkLeaks(bool enabled) { m_checkLeaks = enabled; }
  bool leaksChecked() const { return m_checkLeaks; }

  /** before() + onRun() + after(); throws on a failing leak check */
  void run();

  /** test logic */
  virtual void onRun() = 0;
  /** runs before onRun() */
  virtual void before() {}
  /** runs after onRun(), also when onRun() threw */
  virtual void after() {}
};

struct TestEntry {
  std::string name;
  std::function<std::unique_ptr<Test>()> factory;
};

/** all registered tests (definition order) */
std::vector<TestEntry>& registry();

struct TestRegistrar {
  TestRegistrar(const std::string& name,
                std::function<std::unique_ptr<Test>()> factory);
};

/** run tests, returns the process exit code */
int runFromArgs(int argc, const char* argv[]);

}  // namespace siotest

/** register a Test-derived class under the name @p NAME */
#define SIO_REGISTER_TEST(NAME, CLASS)                                       \
  static ::siotest::TestRegistrar s_testRegistrar_##CLASS(                   \
      #NAME, []() -> std::unique_ptr<::siotest::Test> {                      \
        return std::unique_ptr<::siotest::Test>(new CLASS());                \
      })

#endif /* SIO_TEST_TestRunner_hpp */
