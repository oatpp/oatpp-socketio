#include "oatpp_sio/util.hpp"

#include <cstddef>
#include <random>

namespace {

/** id alphabet, same character set socket.io clients use for their ids */
const char* const c_alphabet = "0123456789abcdefghijklmnopqrstuvwxyz";
const size_t c_alphabetSize = 36;

/**
 * Non-deterministic generator, one instance per thread.
 *
 * This used to be rand(): not thread safe, its sequence restarts at every
 * srand() (so ids were reproducible for anyone who knew the seed - the demo
 * apps even called srand(0xfeedcafe)), and it stole numbers from other users
 * of rand() in the same process.
 */
std::random_device& generator() {
  static thread_local std::random_device rd;
  return rd;
}

}  // namespace

std::string generateRandomString(int length) {

  std::string result;
  if (length <= 0) {
    return result;
  }

  // uniform_int_distribution rejects out-of-range draws -> no modulo bias
  std::uniform_int_distribution<size_t> dist(0, c_alphabetSize - 1);
  auto& rd = generator();

  result.resize(static_cast<size_t>(length));
  for (int i = 0; i < length; i++) {
    result[static_cast<size_t>(i)] = c_alphabet[dist(rd)];
  }

  return result;
}
