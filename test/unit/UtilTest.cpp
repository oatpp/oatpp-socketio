#include "UtilTest.hpp"

#include "TestAssert.hpp"

#include "oatpp_sio/util.hpp"

#include <cstdlib>
#include <set>
#include <string>

void UtilTest::onRun() {

  // -- length is honoured ----------------------------------------------------
  SIO_ASSERT_EQ(generateRandomString(0).size(), size_t(0));
  SIO_ASSERT_EQ(generateRandomString(1).size(), size_t(1));
  SIO_ASSERT_EQ(generateRandomString(6).size(), size_t(6));
  SIO_ASSERT_EQ(generateRandomString(64).size(), size_t(64));

  // -- alphabet is [0-9a-z] --------------------------------------------------
  const std::string sample = generateRandomString(4096);
  SIO_ASSERT_EQ(sample.size(), size_t(4096));
  for (char c : sample) {
    SIO_ASSERT((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z'));
  }

  // -- ids are unique --------------------------------------------------------
  // 12 chars of [0-9a-z] -> 36^12 possibilities, 500 draws must not collide
  std::set<std::string> ids;
  for (int i = 0; i < 500; i++) {
    ids.insert(generateRandomString(12));
  }
  SIO_ASSERT_EQ(ids.size(), size_t(500));

  // -- ids must not be reproducible from a known srand() seed ----------------
  // The old implementation drew from rand(); the demo apps even called
  // srand(0xfeedcafe), so every process produced the same "random" ids.
  srand(1234);
  const std::string first = generateRandomString(16);
  srand(1234);
  const std::string second = generateRandomString(16);
  SIO_ASSERT(first != second);

  // -- id generation must not touch the C runtime RNG ------------------------
  // ... it must also not steal numbers from other users of rand(): the value
  // following a rand() call has to be the same with and without generating
  // ids in between.
  srand(42);
  const int clean1 = rand();
  const int clean2 = rand();

  srand(42);
  const int got1 = rand();
  generateRandomString(16);
  const int got2 = rand();

  SIO_ASSERT_EQ(got1, clean1);
  SIO_ASSERT_EQ(got2, clean2);
}
