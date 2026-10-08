#include "TestRunner.hpp"

#include "TestAssert.hpp"

#include <algorithm>
#include <exception>
#include <iostream>
#include <sstream>

namespace siotest {

void Test::run() {

  const v_counter objectsBefore = oatpp::Environment::getObjectsCount();

  before();

  // after() has to run even when the test failed, so that a test which
  // registered something cleans up; the failure is rethrown afterwards.
  std::exception_ptr pending;
  try {
    onRun();
  } catch (...) {
    pending = std::current_exception();
  }

  after();

  if (pending) {
    std::rethrow_exception(pending);
  }

  if (m_checkLeaks) {
    const v_counter leaked =
        oatpp::Environment::getObjectsCount() - objectsBefore;
    if (leaked != 0) {
      std::ostringstream os;
      os << TAG << ": leaked " << leaked
         << " oatpp object(s) (objects still alive at the end of the test: "
         << oatpp::Environment::getObjectsCount() << ")";
      throw LeakError(os.str());
    }
  }
}

std::vector<TestEntry>& registry() {
  static std::vector<TestEntry> entries;
  return entries;
}

TestRegistrar::TestRegistrar(
    const std::string& name,
    std::function<std::unique_ptr<Test>()> factory) {
  registry().push_back(TestEntry{name, std::move(factory)});
}

int runFromArgs(int argc, const char* argv[]) {

  std::vector<std::string> selection;
  for (int i = 1; i < argc; i++) {
    std::string arg(argv[i]);
    if (arg == "--list") {
      for (const auto& entry : registry()) {
        std::cout << entry.name << "\n";
      }
      return 0;
    }
    if (arg == "--help" || arg == "-h") {
      std::cout << "usage: " << argv[0]
                << " [--list] [TestName ...]\n"
                   "       runs all registered tests if no name is given\n";
      return 0;
    }
    selection.push_back(arg);
  }

  if (selection.empty()) {
    for (const auto& entry : registry()) {
      selection.push_back(entry.name);
    }
  }

  int failed = 0;
  int ran = 0;

  for (const auto& name : selection) {

    auto it = std::find_if(registry().begin(), registry().end(),
                           [&](const TestEntry& e) { return e.name == name; });

    if (it == registry().end()) {
      std::cerr << "ERROR: no such test: '" << name << "' (see --list)\n";
      return 2;
    }

    ran++;
    std::cout << "\n========== RUN " << name << " ==========\n" << std::flush;
    try {
      auto test = it->factory();
      test->run();
      std::cout << "========== PASS " << name << " ==========\n" << std::flush;
    } catch (const siotest::AssertionError& e) {
      std::cerr << "========== FAIL " << name << " ==========\n"
                << "  " << e.what() << "\n" << std::flush;
      failed++;
    } catch (const siotest::LeakError& e) {
      std::cerr << "========== FAIL " << name << " (leak check) ==========\n"
                << "  " << e.what() << "\n" << std::flush;
      failed++;
    } catch (const std::exception& e) {
      std::cerr << "========== FAIL " << name << " (unexpected exception) ==========\n"
                << "  " << e.what() << "\n" << std::flush;
      failed++;
    } catch (...) {
      std::cerr << "========== FAIL " << name << " (unknown exception) ==========\n"
                << std::flush;
      failed++;
    }
  }

  std::cout << "\n----------------------------------------\n"
            << (failed == 0 ? "OK   " : "FAIL ") << ran << " test(s) run, "
            << failed << " failed\n"
            << "----------------------------------------\n";

  return failed == 0 ? 0 : 1;
}

}  // namespace siotest
