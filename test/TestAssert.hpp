/***************************************************************************
 *
 * Assertion helpers for the oatpp-socketio test-suite.
 *
 * `OATPP_ASSERT` (from oatpp) calls exit() on failure, which kills the whole
 * test binary and reports no details. These helpers throw instead, so the
 * runner can report every failed assertion of a test and keep the exit code
 * contract CTest needs.
 *
 ***************************************************************************/

#ifndef SIO_TEST_TestAssert_hpp
#define SIO_TEST_TestAssert_hpp

#include <sstream>
#include <stdexcept>
#include <string>

namespace siotest {

/** thrown by a failing assertion */
class AssertionError : public std::runtime_error {
public:
  explicit AssertionError(const std::string& what) : std::runtime_error(what) {}
};

[[noreturn]] inline void fail(const std::string& where,
                              const std::string& detail) {
  std::ostringstream os;
  os << where << ": " << detail;
  throw AssertionError(os.str());
}

template <class A, class B>
inline void assertEquals(const A& actual, const B& expected,
                         const std::string& where) {
  if (!(actual == expected)) {
    std::ostringstream os;
    os << where << "\n      actual:   [" << actual << "]\n      expected: ["
       << expected << "]";
    throw AssertionError(os.str());
  }
}

}  // namespace siotest

/** fail unless @p EXP is true */
#define SIO_ASSERT(EXP)                                                    \
  do {                                                                     \
    if (!(EXP)) {                                                          \
      ::siotest::fail(std::string(__FILE__) + ":" + std::to_string(__LINE__), \
                      "assertion failed: " #EXP);                          \
    }                                                                      \
  } while (0)

/** fail unless @p EXP is true, with a hand-written explanation */
#define SIO_ASSERT_MSG(EXP, MSG)                                          \
  do {                                                                     \
    if (!(EXP)) {                                                          \
      ::siotest::fail(std::string(__FILE__) + ":" + std::to_string(__LINE__), \
                      std::string("assertion failed: ") + #EXP + "\n      " + (MSG)); \
    }                                                                      \
  } while (0)

/** fail unless ACTUAL == EXPECTED (prints both values) */
#define SIO_ASSERT_EQ(ACTUAL, EXPECTED)                                    \
  ::siotest::assertEquals((ACTUAL), (EXPECTED),                            \
                          std::string(__FILE__) + ":" +                    \
                              std::to_string(__LINE__) + ": " #ACTUAL " == " #EXPECTED)

#endif /* SIO_TEST_TestAssert_hpp */
